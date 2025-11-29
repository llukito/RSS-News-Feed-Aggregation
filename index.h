#ifndef INDEX_H
#define INDEX_H

#include "vector.h"
#include "hashset.h"
#include <stdbool.h>

/* Represents an article */
typedef struct {
    char *url;
    char *title;
    char *server;
} Article;

/* Posting of a word in an article */
typedef struct {
    int article_id;
    int count;
} Posting;

/* WordEntry: word string + postings vector */
typedef struct {
    char *word;          /* lowercase */
    vector postings;     /* vector of Posting */
} WordEntry;

/* Opaque Index structure */
typedef struct index index_t;

/* Lifecycle */
index_t *IndexCreate(int numBuckets);
void IndexDestroy(index_t *idx);

/* Stop words */
bool IndexLoadStopWords(index_t *idx, const char *stopWordsFile);
bool IndexIsStopWord(index_t *idx, const char *word);

/* Articles */
int IndexRegisterArticle(index_t *idx, const char *url, const char *title);
const char *IndexGetArticleTitle(index_t *idx, int article_id);
const char *IndexGetArticleURL(index_t *idx, int article_id);

/* Token insertion */
void IndexAddToken(index_t *idx, int article_id, const char *token);

/* Query */
typedef struct {
    int article_id;
    int count;
} result_t;

int IndexQueryTopN(index_t *idx, const char *word, int topN, vector *outResults);

#endif // INDEX_H
