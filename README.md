# NewsHound: Distributed RSS Indexing Engine

![Language](https://img.shields.io/badge/language-C-blue.svg)
![Type](https://img.shields.io/badge/category-Information_Retrieval-orange.svg)
![Status](https://img.shields.io/badge/status-Legacy_Code-lightgrey.svg)

NewsHound is a high-performance C application that aggregates distributed RSS news feeds, parses live HTML content, and builds an efficient in-memory "Inverted Index" to enable real-time keyword search across hundreds of articles.

This project demonstrates core systems programming concepts, including network data ingestion, markup parsing, and the implementation of custom high-performance data structures (HashSets and Vectors) for O(1) lookups.

## Key Features

- **Automated Aggregation:** Connects to remote servers to download and parse RSS 2.0 XML feeds, extracting article metadata and URLs.
- **Inverted Indexing:** Constructs a mapping of {Keyword -> List of Articles} to allow for instant query resolution, similar to the architecture of large-scale search engines.
- **Relevance Ranking:** Implements a frequency-based ranking algorithm (Term Frequency) to sort search results by relevance.
- **Noise Reduction:** Utilizes a "Stop Word" filtering layer to strip out common grammatical articles (e.g., "the", "and") during indexing, optimizing memory usage and search quality.
- **Deduplication:** Detects and merges duplicate articles syndicated across multiple feeds to ensure result uniqueness.

## Technical Architecture

### 1. The Ingestion Pipeline
The system connects to a list of provided RSS feed URLs. It utilizes a stream tokenizer to parse the incoming XML, identifying `<item>`, `<title>`, and `<link>` tags to extract potential articles.

### 2. The Indexing Engine
Once an article is identified, the engine downloads the raw HTML.
- **Tokenization:** HTML tags are stripped, and the text is broken into individual tokens.
- **Normalization:** Tokens are case-normalized and checked against a `StopWord` HashSet.
- **hashing:** Valid keywords are hashed and stored in a dynamic `HashSet`.
- **Vector Storage:** Each keyword points to a `Vector` of `Article` structs, tracking the frequency of the word in that specific document.

### 3. The Query Interface
Users can perform boolean queries against the index. The system hashes the search term, retrieves the associated `Vector` of articles, and sorts them by frequency count before displaying the results.

## Challenges & Solutions

### Memory Management
Indexing the web is memory-intensive. The system implements rigorous dynamic memory management, ensuring that thousands of string allocations for article titles, URLs, and keywords are properly freed upon shutdown.

### Data Deduplication
News wires often cross-post stories. The engine implements a comparator logic that flags articles as duplicates if they share the same URL or the same Title + Server Origin, preventing index pollution.

## Installation & Usage

### Prerequisites
- GCC Compiler
- Standard C Libraries

### Build
    make

### Usage
Run the aggregator with the provided database of feeds:

    ./rss-search

    > Welcome to NewsHound. Indexing feeds...
    > Indexing complete. 1400 articles indexed.
    > Enter search term: "Linux"

## Project Structure

    ├── src/
    │   ├── rss-search.c       # Main entry point and orchestration
    │   ├── html-parser.c      # Tokenization logic
    │   └── indexer.c          # HashSet and Vector implementation
    ├── data/
    │   └── feeds.txt          # List of RSS streams
    ├── Makefile
    └── README.md

---

**Author:** Luka Aladashvili
