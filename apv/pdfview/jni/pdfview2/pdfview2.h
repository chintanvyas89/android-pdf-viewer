
#ifdef PDFVIEW2_H__
#error PDFVIEW2_H__ can be included only once
#endif


#define PDFVIEW2_H__


#include "fitz.h"
#include "mupdf.h"

#define MAX_BOX_NAME 8

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

/**
 * Holds pdf info.
 */
typedef struct {
    int last_pageno;
    fz_context *ctx;
    fz_document *doc;
    int fileno; /* used only when opening by file descriptor */
    int invalid_password;
    char box[MAX_BOX_NAME + 1];
} pdf_t;


/*
 * Declarations
 */


pdf_t* create_pdf_t();
void free_pdf_t(pdf_t *pdf);
pdf_t* parse_pdf_file(const char *filename, int fileno, const char* password);
pdf_t* get_pdf_from_this(JNIEnv *env, jobject this);
void get_size(JNIEnv *env, jobject size, int *width, int *height);
void save_size(JNIEnv *env, jobject size, int width, int height);
void fix_samples(unsigned char *bytes, unsigned int w, unsigned int h);
void rgb_to_alpha(unsigned char *bytes, unsigned int w, unsigned int h);
int get_page_size(pdf_t *pdf, int pageno, int *width, int *height);
void pdf_android_loghandler(const char *m);
jobject create_find_result(JNIEnv *env);
void set_find_result_page(JNIEnv *env, jobject findResult, int page);
void add_find_result_marker(JNIEnv *env, jobject findResult, int x0, int y0, int x1, int y1);
void add_find_result_to_list(JNIEnv *env, jobject *list, jobject find_result);
int convert_point_pdf_to_apv(pdf_t *pdf, int page, int *x, int *y);
int convert_box_pdf_to_apv(pdf_t *pdf, int page, int rotation, fz_rect *bbox);
int find_next(JNIEnv *env, jobject this, int direction);
pdf_page* get_page(pdf_t *pdf, int pageno);


// #ifdef pro
// jobject create_outline_recursive(JNIEnv *env, jclass outline_class, const fz_outline *outline);
// char* extract_text(pdf_t *pdf, int pageno);
// #endif



