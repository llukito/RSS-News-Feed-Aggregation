/* index.c
 *
 * Skeleton implementation with detailed comments describing what each function
 * must do, memory ownership rules, and implementation notes.
 */

#include "index.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include "streamtokenizer.h"
#include "url.h"

struct index {
    hashset stopWords;
    vector articles;    
    hashset wordMap;    
    
    hashset seen_urls;
    hashset seen_title_server;
};

static const signed long kHashMultiplier = -1664117991L;
static int CStringHash(const void *elemAddr, int numBuckets) {
    /* elemAddr is proaddress of the stored char* */
    char **pp = (char **)elemAddr;
    const char *s = (pp && *pp) ? *pp : ""; // so if they are not NULL
    unsigned long hashcode = 0UL;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        hashcode = hashcode * (unsigned long)kHashMultiplier + (unsigned char)tolower((unsigned char)s[i]);
    }
    /* make sure positive and within [0, numBuckets) */
    if (numBuckets <= 0) return 0;
    return (int)(hashcode % (unsigned long)numBuckets);
}

static int CStringCompare(const void *elemAddr1, const void *elemAddr2){
    char **pp1 = (char **)elemAddr1;
    char **pp2 = (char **)elemAddr2;
    char* s1 = *pp1;
    char* s2 = *pp2;
    return strcasecmp(s1, s2); /* case-insensitive */
}

static void CStringFreeFn(void *elemAddr){
    char **pp = (char **)elemAddr;
    if (!pp) return;
    if (*pp) {
        free(*pp);
        *pp = NULL;
    }
}

// for wordEntry
static int WordEntryHash(const void *elemAddr, int numBuckets){
    WordEntry ** pp = (WordEntry **)elemAddr;
    const char *s = (*pp)->word;
    /* reuse CStringHash-style hashing logic but operate on s */
    unsigned long hashcode = 0UL;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        hashcode = hashcode * (unsigned long)kHashMultiplier + (unsigned char)tolower((unsigned char)s[i]);
    }
    if (numBuckets <= 0) return 0;
    return (int)(hashcode % (unsigned long)numBuckets);
}

static int WordEntryCompare(const void *elemAddr1, const void *elemAddr2){
    WordEntry ** pp1 = (WordEntry **)elemAddr1;
    WordEntry ** pp2 = (WordEntry **)elemAddr2;
    const char *a = (*pp1)->word;
    const char *b = (*pp2)->word;
    return strcasecmp(a, b); // still case sensitive
}

static void WordEntryFreeFn(void *elemAddr){
    WordEntry **pp = (WordEntry **)elemAddr;
    WordEntry* wrd = *pp;
    if (wrd->word) { free(wrd->word); wrd->word = NULL; }
    VectorDispose(&wrd->postings);
    free(wrd);
    *pp = NULL;
}

// for article
static void ArticleFreeFn(void* elem){
    Article* artc = (Article*)elem;
    if (artc->url) { free(artc->url); artc->url = NULL; }
    if (artc->title) { free(artc->title); artc->title = NULL; }
    if (artc->server) { free(artc->server); artc->server = NULL; }
}

index_t *IndexCreate(int numBuckets) {
    if(numBuckets <= 0)numBuckets = 10007;

    index_t* ourIndex = malloc(sizeof(index_t));
    if(!ourIndex)return NULL;

    /* initialize articles */
    VectorNew(&ourIndex->articles, sizeof(Article), ArticleFreeFn, 16);

    /* stopWords */
    HashSetNew(&ourIndex->stopWords, sizeof(char*), 1009, CStringHash, CStringCompare, CStringFreeFn);

    /* wordMap stores WordEntry* pointers */
    HashSetNew(&ourIndex->wordMap, sizeof(WordEntry*), numBuckets, WordEntryHash, WordEntryCompare, WordEntryFreeFn);

    /* duplicate-detection sets */
    HashSetNew(&ourIndex->seen_urls, sizeof(char*), 1009, CStringHash, CStringCompare, CStringFreeFn);
    HashSetNew(&ourIndex->seen_title_server, sizeof(char*), 1009, CStringHash, CStringCompare, CStringFreeFn);

    return ourIndex;
}

void IndexDestroy(index_t *idx) {
    if (idx == NULL) return;

    HashSetDispose(&idx->wordMap);

    HashSetDispose(&idx->stopWords);
    HashSetDispose(&idx->seen_title_server);
    HashSetDispose(&idx->seen_urls);

    VectorDispose(&idx->articles);

    free(idx);
    idx = NULL;
}

static char *StrDupLower(const char *s){
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)s[i]);
    out[n] = '\0';
    return out;
}

bool IndexLoadStopWords(index_t *idx, const char *stopWordsFile) {
    if (idx == NULL || stopWordsFile == NULL) return false;

    FILE *fp = fopen(stopWordsFile, "r");
    if (fp == NULL) return false;

    /* use the same newline delimiters used elsewhere in the project */
    const char *kNewLineDelimiters = "\r\n";
    streamtokenizer st;
    STNew(&st, fp, kNewLineDelimiters, true);

    char token[1024];
    bool ok = true;
    while (STNextToken(&st, token, sizeof(token))) {
        /* Skip empty tokens just in case */
        if (token[0] == '\0') continue;

        /* Lowercase-copy the token (heap-allocated) */
        char *lower = StrDupLower(token);
        if (lower == NULL) {
            ok = false; /* allocation failed */
            break;
        }

        char *p = lower;
        HashSetEnter(&idx->stopWords, &p);
    }

    STDispose(&st);
    fclose(fp);

    if (!ok) {
        return false;
    }
    return true;
}

bool IndexIsStopWord(index_t *idx, const char *word) {
    assert(idx != NULL);
    assert(word != NULL);

    char *lower = StrDupLower(word);
    if(HashSetLookup(&idx->stopWords, &lower) != NULL){
        free(lower);
        return true;
    }   
    free(lower);
    return false;
}

/* ----------------------- Articles -------------------------------------- */

static WordEntry *FindWordEntry(index_t *idx, const char *lowercasedWord);

static const char SERVER_TITLE_SEP = '|';

static char *MakeServerTitleKey(const char *server, const char *title) {
    if (server == NULL) server = "";
    if (title == NULL) title = "";

    size_t l1 = strlen(server);
    size_t l2 = strlen(title);

    /* allocate: server + sep + title + NUL */
    size_t need = l1 + 1 + l2 + 1;
    char *res = malloc(need);
    if (!res) return NULL;

    memcpy(res, server, l1);
    res[l1] = SERVER_TITLE_SEP;
    memcpy(res + l1 + 1, title, l2);
    res[l1 + 1 + l2] = '\0';

    return res;
}

int IndexRegisterArticle(index_t *idx, const char *para_url, const char *title) {
    if(idx == NULL || para_url == NULL)return -1;
    /* --- Step 1: prepare a heap copy of the URL for testing/inserting into seen_urls --- */
    char *copy_para_url = strdup(para_url);
    if (!copy_para_url) return -1;

    /* Check seen_urls (lookup expects address of a char*). Use a temp pointer variable. */
    char *tmp_ptr = copy_para_url;
    if (HashSetLookup(&idx->seen_urls, &tmp_ptr) != NULL) {
        /* already seen */
        free(copy_para_url);
        return -1;
    }

    // no server|title dublicates
    url u;
    URLNewAbsolute(&u, para_url); 
    const char *serverName = u.serverName ? u.serverName : "";

    char* key = MakeServerTitleKey(serverName, title ? title : "");
    if(key == NULL){
        URLDispose(&u);
        free(copy_para_url);
        return -1;
    }

    char* tmp_key = key;
    if(HashSetLookup(&idx->seen_title_server, &tmp_key) != NULL){
        free(key);
        free(copy_para_url);
        URLDispose(&u);
        return -1;
    }
    HashSetEnter(&idx->seen_urls, &copy_para_url);
    HashSetEnter(&idx->seen_title_server, &key);
    
    // it got accepted
    Article art;
    art.url = strdup(para_url);                      /* Article must own its own copy */
    art.title = strdup(title ? title : "");
    art.server = strdup(serverName);   

    if (!art.url || !art.title || !art.server) {
        if (art.url) free(art.url);
        if (art.title) free(art.title);
        if (art.server) free(art.server);
        URLDispose(&u);
        return -1;
    }
    VectorAppend(&idx->articles, &art); // took ownership

    int article_ID = VectorLength(&idx->articles) - 1;
    URLDispose(&u);

    return article_ID;
}

const char *IndexGetArticleTitle(index_t *idx, int article_id) {
    if(idx == NULL || article_id < 0 || article_id >= VectorLength(&idx->articles))return NULL;
    Article* art = (Article *)VectorNth(&idx->articles, article_id);
    return art->title;
}


const char *IndexGetArticleURL(index_t *idx, int article_id) {
    if(idx == NULL || article_id < 0 || article_id >= VectorLength(&idx->articles))return NULL;
    Article* art = (Article *)VectorNth(&idx->articles, article_id);
    return art->url;
}

/* ----------------------- Token insertion -------------------------------- */

void IndexAddToken(index_t *idx, int article_id, const char *token) {
    if(idx == NULL || token == NULL || article_id < 0 || article_id >= VectorLength(&idx->articles)){
        return;
    }

    char* lower = StrDupLower(token);
    if(lower == NULL)return;
    
    char *stop_lookup = lower;
    if(HashSetLookup(&idx->stopWords, &stop_lookup) != NULL){
        free(lower);
        return;
    }

    WordEntry temp;
    temp.word = lower;
    WordEntry *tempPtr = &temp; 

    void* find = HashSetLookup(&idx->wordMap, &tempPtr);

    WordEntry *we = NULL;
    if(find == NULL){
        we = malloc(sizeof(WordEntry));
        if (we == NULL) { free(lower); return; }
        we->word = lower;
        VectorNew(&we->postings, sizeof(Posting), NULL, 16);
        WordEntry *tmp = we; HashSetEnter(&idx->wordMap, &tmp);
    } else {
        WordEntry **stored = (WordEntry **)find;
        we = *stored;
        /* lower was only for lookup; we don't need it any more */
        free(lower);
    }

    for(int i=0; i<VectorLength(&we->postings); i++){
        Posting* pst = (Posting*)VectorNth(&we->postings, i);
        if(pst->article_id == article_id){
            pst->count++;
            return;
        }
    }
    
    Posting newpost;
    newpost.article_id = article_id;
    newpost.count = 1;
    VectorAppend(&we->postings, &newpost);
}

/* ----------------------- Query ----------------------------------------- */

static int result_t_compare(const void *elemAddr1, const void *elemAddr2){
    const result_t *r1 = (const result_t *)elemAddr1;
    const result_t *r2 = (const result_t *)elemAddr2;

    /* primary: count descending */
    if (r2->count != r1->count) return r2->count - r1->count;

    /* tie-break: smaller article_id first */
    return r1->article_id - r2->article_id;
}


int IndexQueryTopN(index_t *idx, const char *word, int topN, vector *outResults) {
    /* Caller expects outResults to be initialized (rss-news-search always
       VectorDispose(&results) after calling us). So ensure it's initialized
       exactly once here if outResults != NULL. */
    if (outResults == NULL) return 0;

    VectorNew(outResults, sizeof(result_t), NULL, 0);

    if (idx == NULL || word == NULL || topN <= 0) {
        /* leave outResults as empty vector for caller to dispose */
        return 0;
    }

    /* lowercased copy of query word */
    char *lower = StrDupLower(word);
    if (!lower) {
        /* allocation failed: leave outResults empty for caller to dispose */
        return 0;
    }

    /* lookup WordEntry in wordMap (we use a temporary WordEntry for lookup) */
    WordEntry temp;
    temp.word = lower;
    WordEntry *tempPtr = &temp;
    void *found = HashSetLookup(&idx->wordMap, &tempPtr);
    if (found == NULL) {
        free(lower);
        /* no such word: outResults stays empty */
        return 0;
    }

    WordEntry *we = *(WordEntry **)found; /* HashSet stores WordEntry* elements */

    /* build a temporary vector of result_t from the postings */
    vector tempVec;
    VectorNew(&tempVec, sizeof(result_t), NULL,
              (VectorLength(&we->postings) > 0) ? VectorLength(&we->postings) : 4);

    for (int i = 0; i < VectorLength(&we->postings); i++) {
        Posting *pst = (Posting *)VectorNth(&we->postings, i);
        result_t r;
        r.article_id = pst->article_id;
        r.count = pst->count;
        VectorAppend(&tempVec, &r);
    }

    free(lower); /* no longer needed */

    if (VectorLength(&tempVec) == 0) {
        VectorDispose(&tempVec);
        return 0; /* outResults remains empty */
    }

    /* sort the contiguous array inside tempVec (vector stores contiguous elems) */
    qsort(tempVec.elems, VectorLength(&tempVec), sizeof(result_t), result_t_compare);

    /* copy topN results into outResults (outResults already init'd) */
    int total = VectorLength(&tempVec);
    int take = (topN < total) ? topN : total;

    for (int i = 0; i < take; ++i) {
        result_t *src = (result_t *)VectorNth(&tempVec, i);
        VectorAppend(outResults, src);
    }

    VectorDispose(&tempVec);
    return VectorLength(outResults);
}

