#include <string.h>
#include <wctype.h>
#include <jni.h>

#include "android/log.h"

#include "pdfview2.h"

#include "mupdf-internal.h"

#define PDFVIEW_LOG_TAG "cx.hell.android.pdfview"


static jintArray get_page_image_bitmap(JNIEnv *env,
      pdf_t *pdf, int pageno, int zoom_pmil, int left, int top, int rotation,
      int skipImages,
      int *width, int *height);
static void copy_alpha(unsigned char* out, unsigned char *in, unsigned int w, unsigned int h);
fz_rect get_page_box(pdf_t *pdf, int pageno);


#define NUM_BOXES 5

const char boxes[NUM_BOXES][MAX_BOX_NAME+1] = {
    "ArtBox",
    "BleedBox",
    "CropBox",
    "MediaBox",
    "TrimBox"
};

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved) {
    __android_log_print(ANDROID_LOG_INFO, PDFVIEW_LOG_TAG, "JNI_OnLoad");
    return JNI_VERSION_1_2;
}


/**
 * Implementation of native method PDF.parseFile.
 * Opens file and parses at least some bytes - so it could take a while.
 * @param file_name file name to parse.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_lib_pdf_PDF_parseFile(
        JNIEnv *env,
        jobject jthis,
        jstring file_name,
        jint box_type,
        jstring password
        ) {
    const char *c_file_name = NULL;
    const char *c_password = NULL;
    jboolean iscopy;
    jclass this_class;
    jfieldID pdf_field_id;
    jfieldID invalid_password_field_id;
    pdf_t *pdf = NULL;

    c_file_name = (*env)->GetStringUTFChars(env, file_name, &iscopy);
    c_password = (*env)->GetStringUTFChars(env, password, &iscopy);
    this_class = (*env)->GetObjectClass(env, jthis);
    pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
    invalid_password_field_id = (*env)->GetFieldID(env, this_class, "invalid_password", "I");
    pdf = parse_pdf_file(c_file_name, 0, c_password);

    if (pdf != NULL && pdf->invalid_password) {
        (*env)->SetIntField(env, jthis, invalid_password_field_id, 1);
        free_pdf_t(pdf);
        pdf = NULL;
    } else {
        (*env)->SetIntField(env, jthis, invalid_password_field_id, 0);
    }

    if (pdf != NULL) {
        if (box_type >= 0 && box_type < NUM_BOXES) {
            strcpy(pdf->box, boxes[box_type]);
        } else {
            strcpy(pdf->box, "CropBox");
        }
    }

    (*env)->ReleaseStringUTFChars(env, file_name, c_file_name);
    (*env)->ReleaseStringUTFChars(env, password, c_password);
    (*env)->SetIntField(env, jthis, pdf_field_id, (int)pdf);
}


/**
 * Create pdf_t struct from opened file descriptor.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_lib_pdf_PDF_parseFileDescriptor(
        JNIEnv *env,
        jobject jthis,
        jobject fileDescriptor,
        jint box_type,
        jstring password
        ) {
    int fileno;
    jclass this_class;
    jfieldID pdf_field_id;
    pdf_t *pdf = NULL;
    jfieldID invalid_password_field_id;
    jboolean iscopy;
    const char* c_password;

    c_password = (*env)->GetStringUTFChars(env, password, &iscopy);
	this_class = (*env)->GetObjectClass(env, jthis);
	pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
    invalid_password_field_id = (*env)->GetFieldID(env, this_class, "invalid_password", "I");

    fileno = get_descriptor_from_file_descriptor(env, fileDescriptor);
	pdf = parse_pdf_file(NULL, fileno, c_password);

    if (pdf != NULL && pdf->invalid_password) {
       (*env)->SetIntField(env, jthis, invalid_password_field_id, 1);
       free_pdf_t(pdf);
       pdf = NULL;
    }
    else {
       (*env)->SetIntField(env, jthis, invalid_password_field_id, 0);
    }

    if (pdf != NULL) {
        if (NUM_BOXES <= box_type)
            strcpy(pdf->box, "CropBox");
        else
            strcpy(pdf->box, boxes[box_type]);
    }
    (*env)->ReleaseStringUTFChars(env, password, c_password);
    (*env)->SetIntField(env, jthis, pdf_field_id, (int)pdf);
}


/**
 * Implementation of native method PDF.getPageCount - return page count of this PDF file.
 * Returns -1 on error, eg if pdf_ptr is NULL.
 * @param env JNI Environment
 * @param this PDF object
 * @return page count or -1 on error
 */
JNIEXPORT jint JNICALL
Java_cx_hell_android_lib_pdf_PDF_getPageCount(
		JNIEnv *env,
		jobject this) {
	pdf_t *pdf = NULL;
    pdf = get_pdf_from_this(env, this);
	if (pdf == NULL) {
        // __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "pdf is null");
        return -1;
    }
	return fz_count_pages(pdf->doc);
}


JNIEXPORT jintArray JNICALL
Java_cx_hell_android_lib_pdf_PDF_renderPage(
        JNIEnv *env,
        jobject this,
        jint pageno,
        jint zoom,
        jint left,
        jint top,
        jint rotation,
        jboolean skipImages,
        jobject size) {

    jint *buf; /* rendered page, freed before return, as bitmap */
    jintArray jints; /* return value */
    pdf_t *pdf; /* parsed pdf data, extracted from java's "this" object */
    int width, height;

    get_size(env, size, &width, &height);

    /*
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "jni renderPage(pageno: %d, zoom: %d, left: %d, top: %d, width: %d, height: %d) start",
            (int)pageno, (int)zoom,
            (int)left, (int)top,
            (int)width, (int)height);
    */

    pdf = get_pdf_from_this(env, this);

    jints = get_page_image_bitmap(env, pdf, pageno, zoom, left, top, rotation, skipImages, &width, &height);

    if (jints != NULL)
        save_size(env, size, width, height);

    return jints;
}


JNIEXPORT jint JNICALL
Java_cx_hell_android_lib_pdf_PDF_getPageSize(
        JNIEnv *env,
        jobject this,
        jint pageno,
        jobject size) {
    int width, height, error;
    pdf_t *pdf = NULL;

    pdf = get_pdf_from_this(env, this);
    if (pdf == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "this.pdf is null");
        return 1;
    }

    error = get_page_size(pdf, pageno, &width, &height);
    if (error != 0) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "get_page_size error: %d", (int)error);
        return 2;
    }

    save_size(env, size, width, height);
    return 0;
}


// #ifdef pro
// /**
//  * Get document outline.
//  */
// JNIEXPORT jobject JNICALL
// Java_cx_hell_android_lib_pdf_PDF_getOutlineNative(
//         JNIEnv *env,
//         jobject this) {
//     int error;
//     pdf_t *pdf = NULL;
//     jobject joutline = NULL;
//     fz_outline *outline = NULL; /* outline root */
//     fz_outline *curr_outline = NULL; /* for walking over fz_outline tree */
// 
//     pdf = get_pdf_from_this(env, this);
//     if (pdf == NULL) {
//         __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "this.pdf is null");
//         return NULL;
//     }
// 
//     outline = fz_load_outline(pdf->doc);
//     if (outline == NULL) return NULL;
// 
//     /* recursively copy fz_outline to PDF.Outline */
//     /* TODO: rewrite pdf_load_outline to create Java's PDF.Outline objects directly */
//     joutline = create_outline_recursive(env, NULL, outline);
//     // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "joutline converted");
//     return joutline;
// }
// #endif


/**
 * Free resources allocated in native code.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_lib_pdf_PDF_freeMemory(
        JNIEnv *env,
        jobject this) {
    pdf_t *pdf = NULL;
	jclass this_class = (*env)->GetObjectClass(env, this);
	jfieldID pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "jni freeMemory()");
	pdf = (pdf_t*) (*env)->GetIntField(env, this, pdf_field_id);
	(*env)->SetIntField(env, this, pdf_field_id, 0);
    free_pdf_t(pdf);
}


#if 0
JNIEXPORT void JNICALL
Java_cx_hell_android_pdfview_PDF_export(
        JNIEnv *env,
        jobject this) {
    pdf_t *pdf = NULL;
    jobject results = NULL;
    pdf_page *page = NULL;
    fz_text_span *text_span = NULL, *ln = NULL;
    fz_device *dev = NULL;
    char *textlinechars;
    char *found = NULL;
    fz_error error = 0;
    jobject find_result = NULL;
    int pageno = 0;
    int pagecount;
    int fd;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "export to txt");

    pdf = get_pdf_from_this(env, this);

    pagecount = pdf_count_pages(pdf->xref);

    fd = open("/tmp/pdfview-export.txt", O_WRONLY|O_CREAT, 0666);
    if (fd < 0) {
         __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "Error opening /tmp/pdfview-export.txt");
        return;
    }

    for(pageno = 0; pageno < pagecount ; pageno++) {
        page = get_page(pdf, pageno);

        if (pdf->last_pageno != pageno && NULL != pdf->xref->store) {
            pdf_age_store(pdf->xref->store, TEXT_STORE_MAX_AGE);
            pdf->last_pageno = pageno;
        }

      text_span = fz_new_text_span();
      dev = fz_new_text_device(text_span);
      error = pdf_run_page(pdf->xref, page, dev, fz_identity);
      if (error)
      {
          /* TODO: cleanup */
          fz_rethrow(error, "text extraction failed");
          return;
      }

      /* TODO: Detect paragraph breaks using bbox field */
      for(ln = text_span; ln; ln = ln->next) {
          int i;
          textlinechars = (char*)malloc(ln->len + 1);
          for(i = 0; i < ln->len; ++i) textlinechars[i] = ln->text[i].c;
          textlinechars[i] = '\n';
          write(fd, textlinechars, ln->len+1);
          free(textlinechars);
      }

      fz_free_device(dev);
      fz_free_text_span(text_span);
    }

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "export complete");

    close(fd);
}
#endif


/* wcsstr() seems broken--it matches too much */
wchar_t* widestrstr(wchar_t* haystack, int haystack_length, wchar_t* needle, int needle_length) {
    char* found;
    int byte_haystack_length;
    int byte_needle_length;

    if (needle_length == 0)
         return haystack;
         
    byte_haystack_length = haystack_length * sizeof(wchar_t);
    byte_needle_length = needle_length * sizeof(wchar_t);

    while(haystack_length >= needle_length &&
        NULL != (found = memmem(haystack, byte_haystack_length, needle, byte_needle_length))) {
          int delta = found - (char*)haystack;
          int new_offset;

          /* Check if the find is wchar_t-aligned */
          if (delta % sizeof(wchar_t) == 0)
              return (wchar_t*)found;

          new_offset = (delta + sizeof(wchar_t) - 1) / sizeof(wchar_t);

          haystack += new_offset;
          haystack_length -= new_offset;
          byte_haystack_length = haystack_length * sizeof(wchar_t);
    }

    return NULL;
}

/* TODO: Specialcase searches for 7-bit text to make them faster */
JNIEXPORT jobject JNICALL
Java_cx_hell_android_lib_pdf_PDF_find(
        JNIEnv *env,
        jobject this,
        jstring text,
        jint pageno,
        jint rotation) {
    int i = 0;
    pdf_t *pdf = NULL;
    const jchar *jtext = NULL;
    wchar_t *ctext = NULL;
    size_t needle_len = 0;
    jboolean is_copy;
    jobject results = NULL;
    fz_rect pagebox;
    fz_page *page = NULL;
    fz_text_sheet *sheet = NULL;
    fz_text_page *text_page = NULL;  /* contains text */
    fz_device *dev = NULL;
    wchar_t *textlinechars = NULL;
    size_t textlinechars_len = 0; /* not including \0 */
    fz_rect *textlineboxes = NULL; /* array of boxes of textlinechars, has textlinechars_len elements */
    wchar_t *found = NULL;
    jobject find_result = NULL;
    fz_text_block *text_block = NULL;
    fz_text_line *text_line = NULL;
    fz_text_span *text_span = NULL;
    fz_text_char *text_char = NULL;
    int block_no = 0;
    int line_no = 0;
    int char_no = 0;
    int span_no = 0;

    jtext = (*env)->GetStringChars(env, text, &is_copy);

    if (jtext == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "text cannot be null");
        (*env)->ReleaseStringChars(env, text, jtext);
        return NULL;
    }

    needle_len = (*env)->GetStringLength(env, text);

    ctext = malloc((needle_len + 1) * sizeof(wchar_t));
    for (i=0; i<needle_len; i++) {
        ctext[i] = towlower(jtext[i]);
    }
    ctext[needle_len] = 0; /* This will be needed if wcsstr() ever starts to work */

    pdf = get_pdf_from_this(env, this);

    page = fz_load_page(pdf->doc, pageno);
    sheet = fz_new_text_sheet(pdf->ctx);
    pagebox = get_page_box(pdf, pageno);
    text_page = fz_new_text_page(pdf->ctx, pagebox);
    dev = fz_new_text_device(pdf->ctx, sheet, text_page);

    fz_run_page(pdf->doc, page, dev, fz_identity, NULL);

    /* search text_page by extracting wchar_t text for each line */
    for(block_no = 0; block_no < text_page->len; ++block_no) {  /* for each block */
        // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "checking block %d of %d", block_no, text_page->len);
        text_block = &(text_page->blocks[block_no]);
        for(line_no = 0; line_no < text_block->len; ++line_no) {  /* for each line */
            // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "checking line %d of %d", line_no, text_block->len);
            text_line = &(text_block->lines[line_no]);
            /* cound chars in line */
            textlinechars_len = 0;
            for(span_no = 0; span_no < text_line->len; ++span_no) {
                textlinechars_len += text_line->spans[span_no].len;
            }
            // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "text line chars len: %d", textlinechars_len);
            textlinechars = (wchar_t*)malloc((textlinechars_len + 1) * sizeof(wchar_t));
            textlineboxes = (fz_rect*)malloc(textlinechars_len * sizeof(fz_rect));
            /* copy chars and boxes */
            char_no = 0;
            for(span_no = 0; span_no < text_line->len; ++span_no) {
                int span_char_no = 0;
                text_span = &(text_line->spans[span_no]);
                for(span_char_no = 0; span_char_no < text_span->len; ++span_char_no) {
                    textlinechars[char_no] = towlower(text_span->text[span_char_no].c);
                    textlineboxes[char_no] = text_span->text[span_char_no].bbox;
                    char_no += 1;
                }
            }
            textlinechars[textlinechars_len] = 0;

            // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "copied textlinechars");
            found = widestrstr(textlinechars, textlinechars_len, ctext, needle_len);
            // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "widestrstr result: %d", (int)found);
            if (found) {
                int i = 0; /* used for char in textlinechars */
                int i0 = 0, i1 = 0; /* indexes of match in textlinechars */
                find_result = create_find_result(env);
                set_find_result_page(env, find_result, pageno);
                fz_rect charbox;
                i0 = found - textlinechars;
                i1 = i0 + needle_len;
                // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "copying chars from %d to %d", i0, i1);
                for(i = i0; i < i1; ++i) {
                    charbox = textlineboxes[i];
                    convert_box_pdf_to_apv(pdf, pageno, rotation, &charbox);
                    add_find_result_marker(env, find_result,
                            charbox.x0, charbox.y0,
                            charbox.x1, charbox.y1);
                }
                // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "adding find result to list");
                add_find_result_to_list(env, &results, find_result);
                // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "added find result to list");
            }

            // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "freeing textlinechars and textlineboxes");
            free(textlinechars);
            textlinechars = NULL;
            free(textlineboxes);
            textlineboxes = NULL;
        }
    }

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "freeing text_page, sheet, dev");
    // fz_free_text_page(pdf->ctx, text_page);
    // fz_free_device(dev);

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "freeing ctext");
    free(ctext);
    (*env)->ReleaseStringChars(env, text, jtext);
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "returning results");
    return results;
}




// #ifdef pro
// /**
//  * Return text of given page.
//  */
// JNIEXPORT jobject JNICALL
// Java_cx_hell_android_lib_pdf_PDF_getText(
//         JNIEnv *env,
//         jobject this,
//         jint pageno) {
//     char *text = NULL;
//     pdf_t *pdf = NULL;
//     pdf = get_pdf_from_this(env, this);
//     jstring jtext = NULL;
// 
//     if (pdf == NULL) {
//         __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "getText: pdf is NULL");
//         return NULL;
//     }
//     text = extract_text(pdf, pageno);
//     jtext = (*env)->NewStringUTF(env, text);
//     if (text) free(text);
//     return jtext;
// }
// #endif


/**
 * Create empty FindResult object.
 * @param env JNI Environment
 * @return newly created, empty FindResult object
 */
jobject create_find_result(JNIEnv *env) {
    static jmethodID constructorID;
    jclass findResultClass = NULL;
    static int jni_ids_cached = 0;
    jobject findResultObject = NULL;

    findResultClass = (*env)->FindClass(env, "cx/hell/android/lib/pagesview/FindResult");

    if (findResultClass == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_find_result: FindClass returned NULL");
        return NULL;
    }

    if (jni_ids_cached == 0) {
        constructorID = (*env)->GetMethodID(env, findResultClass, "<init>", "()V");
        if (constructorID == NULL) {
            (*env)->DeleteLocalRef(env, findResultClass);
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_find_result: couldn't get method id for FindResult constructor");
            return NULL;
        }
        jni_ids_cached = 1;
    }

    findResultObject = (*env)->NewObject(env, findResultClass, constructorID);
    (*env)->DeleteLocalRef(env, findResultClass);
    return findResultObject;
}


void add_find_result_to_list(JNIEnv *env, jobject *list, jobject find_result) {
    static int jni_ids_cached = 0;
    static jmethodID list_add_method_id = NULL;
    jclass list_class = NULL;
    if (list == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "list cannot be null - it must be a pointer jobject variable");
        return;
    }
    if (find_result == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "find_result cannot be null");
        return;
    }
    if (*list == NULL) {
        jmethodID list_constructor_id;
        // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "creating ArrayList");
        list_class = (*env)->FindClass(env, "java/util/ArrayList");
        if (list_class == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't find class java/util/ArrayList");
            return;
        }
        list_constructor_id = (*env)->GetMethodID(env, list_class, "<init>", "()V");
        if (!list_constructor_id) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't find ArrayList constructor");
            return;
        }
        *list = (*env)->NewObject(env, list_class, list_constructor_id);
        if (*list == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to create ArrayList: NewObject returned NULL");
            return;
        }
    }

    if (!jni_ids_cached) {
        if (list_class == NULL) {
            list_class = (*env)->FindClass(env, "java/util/ArrayList");
            if (list_class == NULL) {
                __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't find class java/util/ArrayList");
                return;
            }
        }
        list_add_method_id = (*env)->GetMethodID(env, list_class, "add", "(Ljava/lang/Object;)Z");
        if (list_add_method_id == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't get ArrayList.add method id");
            return;
        }
        jni_ids_cached = 1;
    } 

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "calling ArrayList.add");
    (*env)->CallBooleanMethod(env, *list, list_add_method_id, find_result);
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "add_find_result_to_list done");
}


/**
 * Set find results page member.
 * @param JNI environment
 * @param findResult find result object that should be modified
 * @param page new value for page field
 */
void set_find_result_page(JNIEnv *env, jobject findResult, int page) {
    static char jni_ids_cached = 0;
    static jfieldID page_field_id = 0;
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "trying to set find results page number");
    if (jni_ids_cached == 0) {
        jclass findResultClass = (*env)->GetObjectClass(env, findResult);
        page_field_id = (*env)->GetFieldID(env, findResultClass, "page", "I");
        jni_ids_cached = 1;
    }
    (*env)->SetIntField(env, findResult, page_field_id, page);
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "find result page number set");
}


/**
 * Add marker to find result.
 */
void add_find_result_marker(JNIEnv *env, jobject findResult, int x0, int y0, int x1, int y1) {
    static jmethodID addMarker_methodID = 0;
    static unsigned char jni_ids_cached = 0;
    if (!jni_ids_cached) {
        jclass findResultClass = NULL;
        findResultClass = (*env)->FindClass(env, "cx/hell/android/lib/pagesview/FindResult");
        if (findResultClass == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "add_find_result_marker: FindClass returned NULL");
            return;
        }
        addMarker_methodID = (*env)->GetMethodID(env, findResultClass, "addMarker", "(IIII)V");
        if (addMarker_methodID == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "add_find_result_marker: couldn't find FindResult.addMarker method ID");
            return;
        }
        jni_ids_cached = 1;
    }
    (*env)->CallVoidMethod(env, findResult, addMarker_methodID, x0, y0, x1, y1); /* TODO: is always really int jint? */
}


/**
 * Get pdf_ptr field value, cache field address as a static field.
 * @param env Java JNI Environment
 * @param this object to get "pdf_ptr" field from
 * @return pdf_ptr field value
 */
pdf_t* get_pdf_from_this(JNIEnv *env, jobject this) {
    static jfieldID field_id = 0;
    static unsigned char field_is_cached = 0;
    pdf_t *pdf = NULL;
    if (!field_is_cached) {
        jclass this_class = (*env)->GetObjectClass(env, this);
        field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
        field_is_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached pdf_ptr field id %d", (int)field_id);
    }
	pdf = (pdf_t*) (*env)->GetIntField(env, this, field_id);
    return pdf;
}


/**
 * Get descriptor field value from FileDescriptor class, cache field offset.
 * This is undocumented private field.
 * @param env JNI Environment
 * @param this FileDescriptor object
 * @return file descriptor field value
 */
int get_descriptor_from_file_descriptor(JNIEnv *env, jobject this) {
    static jfieldID field_id = 0;
    static unsigned char is_cached = 0;
    if (!is_cached) {
        jclass this_class = (*env)->GetObjectClass(env, this);
        field_id = (*env)->GetFieldID(env, this_class, "descriptor", "I");
        is_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached descriptor field id %d", (int)field_id);
    }
    // __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "will get descriptor field...");
    return (*env)->GetIntField(env, this, field_id);
}


void get_size(JNIEnv *env, jobject size, int *width, int *height) {
    static jfieldID width_field_id = 0;
    static jfieldID height_field_id = 0;
    static unsigned char fields_are_cached = 0;
    if (! fields_are_cached) {
        jclass size_class = (*env)->GetObjectClass(env, size);
        width_field_id = (*env)->GetFieldID(env, size_class, "width", "I");
        height_field_id = (*env)->GetFieldID(env, size_class, "height", "I");
        fields_are_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached Size fields");
    }
    *width = (*env)->GetIntField(env, size, width_field_id);
    *height = (*env)->GetIntField(env, size, height_field_id);
}


/**
 * Store width and height values into PDF.Size object, cache field ids in static members.
 * @param env JNI Environment
 * @param width width to store
 * @param height height field value to be stored
 * @param size target PDF.Size object
 */
void save_size(JNIEnv *env, jobject size, int width, int height) {
    static jfieldID width_field_id = 0;
    static jfieldID height_field_id = 0;
    static unsigned char fields_are_cached = 0;
    if (! fields_are_cached) {
        jclass size_class = (*env)->GetObjectClass(env, size);
        width_field_id = (*env)->GetFieldID(env, size_class, "width", "I");
        height_field_id = (*env)->GetFieldID(env, size_class, "height", "I");
        fields_are_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached Size fields");
    }
    (*env)->SetIntField(env, size, width_field_id, width);
    (*env)->SetIntField(env, size, height_field_id, height);
}


/**
 * pdf_t "constructor": create empty pdf_t with default values.
 * @return newly allocated pdf_t struct with fields set to default values
 */
pdf_t* create_pdf_t() {
    pdf_t *pdf = NULL;
    pdf = (pdf_t*)malloc(sizeof(pdf_t));

    pdf->ctx = NULL;
    pdf->doc = NULL;
    pdf->fileno = -1;
    pdf->invalid_password = 0;

    pdf->box[0] = 0;
    
    return pdf;
}


/**
 * free pdf_t
 */
void free_pdf_t(pdf_t *pdf) {
    if (pdf->doc) {
        fz_close_document(pdf->doc);
        pdf->doc = NULL;
    }
    if (pdf->ctx) {
        fz_free_context(pdf->ctx);
        pdf->ctx = NULL;
    }
    free(pdf);
}



#if 0
/**
 * Parse bytes into PDF struct.
 * @param bytes pointer to bytes that should be parsed
 * @param len length of byte buffer
 * @return initialized pdf_t struct; or NULL if loading failed
 */
pdf_t* parse_pdf_bytes(unsigned char *bytes, size_t len, jstring box_name) {
    pdf_t *pdf;
    const char* c_box_name;
    fz_error error;

    pdf = create_pdf_t();
    c_box_name = (*env)->GetStringUTFChars(env, box_name, &iscopy);
    strncpy(pdf->box, box_name, 9);
    pdf->box[MAX_BOX_NAME] = 0;

    pdf->xref = pdf_newxref();
    error = pdf_loadxref_mem(pdf->xref, bytes, len);
    if (error) {
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "got err from pdf_loadxref_mem: %d", (int)error);
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "fz errors:\n%s", fz_errorbuf);
        /* TODO: free resources */
        return NULL;
    }

    error = pdf_decryptxref(pdf->xref);
    if (error) {
        return NULL;
    }

    if (pdf_needspassword(pdf->xref)) {
        int authenticated = 0;
        authenticated = pdf_authenticatepassword(pdf->xref, "");
        if (!authenticated) {
            /* TODO: ask for password */
            __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "failed to authenticate with empty password");
            return NULL;
        }
    }

    pdf->xref->root = fz_resolveindirect(fz_dictgets(pdf->xref->trailer, "Root"));
    fz_keepobj(pdf->xref->root);

    pdf->xref->info = fz_resolveindirect(fz_dictgets(pdf->xref->trailer, "Info"));
    fz_keepobj(pdf->xref->info);

    pdf->outline = pdf_loadoutline(pdf->xref);

    return pdf;
}
#endif


/**
 * Parse file into PDF struct.
 * Use filename if it's not null, otherwise use fileno.
 */
pdf_t* parse_pdf_file(const char *filename, int fileno, const char* password) {
    pdf_t *pdf;
    fz_stream *stream = NULL;

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "parse_pdf_file(%s, %d)", filename, fileno);

    pdf = create_pdf_t();

    if (pdf->ctx == NULL) {
        pdf->ctx = fz_new_context(NULL, NULL, 1024 * 1024);
    }

    if (filename) {
        stream = fz_open_file(pdf->ctx, (char*)filename);
    } else {
        stream = fz_open_fd(pdf->ctx, fileno);
    }
    pdf->doc = (fz_document*) pdf_open_document_with_stream(stream);
    fz_close(stream); /* pdf->doc holds ref */

    pdf->invalid_password = 0;

    if (fz_needs_password(pdf->doc)) {
        int authenticated = 0;
        authenticated = fz_authenticate_password(pdf->doc, (char*)password);
        if (!authenticated) {
            /* TODO: ask for password */
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to authenticate");
            pdf->invalid_password = 1;
            return pdf;
        }
    }
    
    pdf->last_pageno = -1;
    return pdf;
}


/**
 * Calculate zoom to best match given dimensions.
 * There's no guarantee that page zoomed by resulting zoom will fit rectangle max_width x max_height exactly.
 * @param max_width expected max width
 * @param max_height expected max height
 * @param page original page
 * @return zoom required to best fit page into max_width x max_height rectangle
 */
/*double get_page_zoom(pdf_page *page, int max_width, int max_height) {
    double page_width, page_height;
    double zoom_x, zoom_y;
    double zoom;
    page_width = page->mediabox.x1 - page->mediabox.x0;
    page_height = page->mediabox.y1 - page->mediabox.y0;

    zoom_x = max_width / page_width;
    zoom_y = max_height / page_height;

    zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;

    return zoom;
}*/


/**
 * Get part of page as bitmap.
 * Parameters left, top, width and height are interprted after scalling, so if
 * we have 100x200 page scalled by 25% and request 0x0 x 25x50 tile, we should
 * get 25x50 bitmap of whole page content. pageno is 0-based.
 */
static jintArray get_page_image_bitmap(JNIEnv *env,
      pdf_t *pdf, int pageno, int zoom_pmil, int left, int top, int rotation,
      int skipImages,
      int *width, int *height) {
    unsigned char *bytes = NULL;
    fz_matrix ctm;
    double zoom;
    fz_bbox bbox;
    fz_page *page = NULL;
    fz_pixmap *image = NULL;
    static int runs = 0;
    fz_device *dev = NULL;
    int num_pixels;
    jintArray jints; /* return value */
    int *jbuf; /* pointer to internal jint */

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "get_page_image_bitmap(pageno: %d) start", (int)pageno);

    zoom = (double)zoom_pmil / 1000.0;

    if (pdf->last_pageno != pageno) {
        pdf->last_pageno = pageno;
    }

    page = fz_load_page(pdf->doc, pageno);
    if (!page) return NULL; /* TODO: handle/propagate errors */

    fz_rect pagebox = get_page_box(pdf, pageno);

    /* translate coords to apv coords so we can easily cut out our tile */
    ctm = fz_identity;
    ctm = fz_concat(ctm, fz_scale(zoom, zoom));
    if (rotation != 0) ctm = fz_concat(ctm, fz_rotate(-rotation * 90));
    bbox = fz_round_rect(fz_transform_rect(ctm, pagebox));

    /* now bbox holds page after transform, but we only need tile at (left,right) from top-left corner */
    bbox.x0 = bbox.x0 + left;
    bbox.y0 = bbox.y0 + top;
    bbox.x1 = bbox.x0 + *width;
    bbox.y1 = bbox.y0 + *height;

    image = fz_new_pixmap_with_bbox(pdf->ctx, fz_device_bgr, bbox);
    fz_clear_pixmap_with_value(pdf->ctx, image, 0xff);
    dev = fz_new_draw_device(pdf->ctx, image);

    if (skipImages)
        dev->hints |= FZ_IGNORE_IMAGE;

    fz_run_page(pdf->doc, page, dev, ctm, NULL);
    fz_free_device(dev);

    /*
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got image %d x %d, asked for %d x %d",
            fz_pixmap_width(pdf->ctx, image), fz_pixmap_height(pdf->ctx, image),
            *width, *height);
    */

    /* TODO: learn jni and avoid copying bytes ;) */
    num_pixels = fz_pixmap_width(pdf->ctx, image) * fz_pixmap_height(pdf->ctx, image);
    jints = (*env)->NewIntArray(env, num_pixels);
	jbuf = (*env)->GetIntArrayElements(env, jints, NULL);
    memcpy(jbuf, fz_pixmap_samples(pdf->ctx, image), num_pixels * 4);
    (*env)->ReleaseIntArrayElements(env, jints, jbuf, 0);

    *width = fz_pixmap_width(pdf->ctx, image);
    *height = fz_pixmap_height(pdf->ctx, image);
    fz_drop_pixmap(pdf->ctx, image);
	fz_free_page(pdf->doc, page);
    runs += 1;
    return jints;
}

/**
 * Get page size in APV's convention.
 * @param page 0-based page number
 * @param pdf pdf struct
 * @param width target for width value
 * @param height target for height value
 * @return error code - 0 means ok
 */
int get_page_size(pdf_t *pdf, int pageno, int *width, int *height) {
    fz_rect rect = get_page_box(pdf, pageno);
    *width = rect.x1 - rect.x0;
    *height = rect.y1 - rect.y0;
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "get_page_size(%d) -> %d %d", pageno, *width, *height);
    return 0;
}


/**
 * Get page box.
 */
fz_rect get_page_box(pdf_t *pdf, int pageno) {
    fz_rect box;
    fz_page *page = NULL;
    if (pdf->box && pdf->box[0] && strcmp(pdf->box, "MediaBox") != 0) {
        /* only get box this way if pdf->box and pdf->box != "MediaBox" */
        // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "getting page box using pdf_dict_gets (pdf->box: %s)", pdf->box);
        pdf_obj *pageobj = NULL;
        pdf_obj *obj = NULL;
        pdf_document *xref = NULL;
        xref = (pdf_document*)pdf->doc;
        pageobj = xref->page_objs[pageno];
        obj = pdf_dict_gets(pageobj, pdf->box);
        if (obj && pdf_is_array(obj)) {
            box = pdf_to_rect(pdf->ctx, obj);
            obj = pdf_dict_gets(pageobj, "UserUnit");
            if (pdf_is_real(obj)) {
                float unit = pdf_to_real(obj);
                box.x0 *= unit;
                box.y0 *= unit;
                box.x1 *= unit;
                box.y1 *= unit;
            }
            return box;
        }
    }
    /* if above didn't return... */
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "getting page box using fz_bound_page (pdf->box: %s)", pdf->box);
    page = fz_load_page(pdf->doc, pageno);
    if (!page) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG,
                "fz_load_page(..., %d) -> NULL", pageno);
        return;
    }
    box = fz_bound_page(pdf->doc, page);
    fz_free_page(pdf->doc, page);
    /*
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG,
            "got page %d box: %.2f %.2f %.2f %.2f",
            pageno, box.x0, box.y0, box.x1, box.y1);
    */
    return box;
}


#if 0
/**
 * Convert coordinates from pdf to APVs.
 * TODO: faster? lazy?
 * @return error code, 0 means ok
 */
int convert_point_pdf_to_apv(pdf_t *pdf, int page, int *x, int *y) {
    fz_error error = 0;
    fz_obj *pageobj = NULL;
    fz_obj *rotateobj = NULL;
    fz_obj *sizeobj = NULL;
    fz_rect bbox;
    int rotate = 0;
    fz_point p;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "convert_point_pdf_to_apv()");

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "trying to convert %d x %d to APV coords", *x, *y);

    pageobj = pdf_getpageobject(pdf->xref, page+1);
    if (!pageobj) return -1;
    sizeobj = fz_dictgets(pageobj, pdf->box);
    if (sizeobj == NULL)
        sizeobj = fz_dictgets(pageobj, "MediaBox");
    if (!sizeobj) return -1;
    bbox = pdf_torect(sizeobj);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page bbox is %.1f, %.1f, %.1f, %.1f", bbox.x0, bbox.y0, bbox.x1, bbox.y1);
    rotateobj = fz_dictgets(pageobj, "Rotate");
    if (fz_isint(rotateobj)) {
        rotate = fz_toint(rotateobj);
    } else {
        rotate = 0;
    }
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "rotate is %d", (int)rotate);

    p.x = *x;
    p.y = *y;

    if (rotate != 0) {
        fz_matrix m;
        m = fz_rotate(-rotate);
        bbox = fz_transformrect(m, bbox);
        p = fz_transformpoint(m, p);
    }

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate bbox is: %.1f, %.1f, %.1f, %.1f", bbox.x0, bbox.y0, bbox.x1, bbox.y1);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate point is: %.1f, %.1f", p.x, p.y);

    *x = p.x - MIN(bbox.x0,bbox.x1);
    *y = MAX(bbox.y1, bbox.y0) - p.y;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "result is: %d, %d", *x, *y);

    return 0;
}
#endif


/**
 * Convert coordinates from pdf to APV.
 * Result is stored in location pointed to by bbox param.
 * This function has to get page box relative to which bbox is located.
 * This function should not allocate any memory.
 * @return error code, 0 means ok
 */
int convert_box_pdf_to_apv(pdf_t *pdf, int page, int rotation, fz_rect *bbox) {
    fz_rect page_bbox;
    fz_rect param_bbox;
    float height = 0;
    float width = 0;

    /*
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG,
            "convert_box_pdf_to_apv(page: %d, bbox: %.2f %.2f %.2f %.2f)",
            page, bbox->x0, bbox->y0, bbox->x1, bbox->y1);
    */

    param_bbox = *bbox;

    page_bbox = get_page_box(pdf, page);
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page bbox is %.1f, %.1f, %.1f, %.1f", page_bbox.x0, page_bbox.y0, page_bbox.x1, page_bbox.y1);

    if (rotation != 0) {
        fz_matrix m;
        m = fz_rotate(-rotation * 90);
        param_bbox = fz_transform_rect(m, param_bbox);
        page_bbox = fz_transform_rect(m, page_bbox);
    }

    //__android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate page bbox is: %.1f, %.1f, %.1f, %.1f", page_bbox.x0, page_bbox.y0, page_bbox.x1, page_bbox.y1);
    //__android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate param bbox is: %.1f, %.1f, %.1f, %.1f", param_bbox.x0, param_bbox.y0, param_bbox.x1, param_bbox.y1);

    /* set result: param bounding box relative to left-top corner of page bounding box */

    bbox->x0 = MIN(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1);
    bbox->y0 = MIN(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1);
    bbox->x1 = MAX(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1);
    bbox->y1 = MAX(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1);

    /*
    width = ABS(page_bbox.x0 - page_bbox.x1);
    height = ABS(page_bbox.y0 - page_bbox.y1);

    bbox->x0 = (MIN(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1));
    bbox->y1 = height - (MIN(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1));
    bbox->x1 = (MAX(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1));
    bbox->y0 = height - (MAX(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1));
    */

    /*
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG,
            "result after transformations: %.2f, %.2f, %.2f, %.2f",
            bbox->x0, bbox->y0, bbox->x1, bbox->y1);
    */

    return 0;
}


// #ifdef pro
// jobject create_outline_recursive(JNIEnv *env, jclass outline_class, const fz_outline *outline) {
//     static int jni_ids_cached = 0;
//     static jmethodID constructor_id = NULL;
//     static jfieldID title_field_id = NULL;
//     static jfieldID page_field_id = NULL;
//     static jfieldID next_field_id = NULL;
//     static jfieldID down_field_id = NULL;
//     int outline_class_found = 0;
//     jobject joutline = NULL;
//     jstring jtitle = NULL;
// 
//     if (outline == NULL) return NULL;
// 
//     if (outline_class == NULL) {
//         outline_class = (*env)->FindClass(env, "cx/hell/android/lib/pdf/PDF$Outline");
//         if (outline_class == NULL) {
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "can't find outline class");
//             return NULL;
//         }
//         outline_class_found = 1;
//     }
// 
//     if (!jni_ids_cached) {
//         constructor_id = (*env)->GetMethodID(env, outline_class, "<init>", "()V");
//         if (constructor_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get method id for Outline constructor");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got constructor id");
//         title_field_id = (*env)->GetFieldID(env, outline_class, "title", "Ljava/lang/String;");
//         if (title_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.title");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got title field id");
//         page_field_id = (*env)->GetFieldID(env, outline_class, "page", "I");
//         if (page_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.page");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got page field id");
//         next_field_id = (*env)->GetFieldID(env, outline_class, "next", "Lcx/hell/android/lib/pdf/PDF$Outline;");
//         if (next_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.next");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got down field id");
//         down_field_id = (*env)->GetFieldID(env, outline_class, "down", "Lcx/hell/android/lib/pdf/PDF$Outline;");
//         if (down_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.down");
//             return NULL;
//         }
// 
//         jni_ids_cached = 1;
//     }
// 
//     joutline = (*env)->NewObject(env, outline_class, constructor_id);
//     if (joutline == NULL) {
//         (*env)->DeleteLocalRef(env, outline_class);
//         __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to create joutline");
//         return NULL;
//     }
//     // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "joutline created");
//     if (outline->title) {
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "title to set: %s", outline->title);
//         jtitle = (*env)->NewStringUTF(env, outline->title);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "jtitle created");
//         (*env)->SetObjectField(env, joutline, title_field_id, jtitle);
//         (*env)->DeleteLocalRef(env, jtitle);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "title set");
//     } else {
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "title is null, won't create not set");
//     }
//     if (outline->dest.kind == FZ_LINK_GOTO) {
//         (*env)->SetIntField(env, joutline, page_field_id, outline->dest.ld.gotor.page);
//     } else {
//         __android_log_print(ANDROID_LOG_WARN, PDFVIEW_LOG_TAG, "outline contains non-GOTO link");
//         (*env)->SetIntField(env, joutline, page_field_id, -1);
//     }
//     // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page set");
//     if (outline->next) {
//         jobject next_outline = NULL;
//         next_outline = create_outline_recursive(env, outline_class, outline->next);
//         (*env)->SetObjectField(env, joutline, next_field_id, next_outline);
//         (*env)->DeleteLocalRef(env, next_outline);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "next set");
//     }
//     if (outline->down) {
//         jobject down_outline = NULL;
//         down_outline = create_outline_recursive(env, outline_class, outline->down);
//         (*env)->SetObjectField(env, joutline, down_field_id, down_outline);
//         (*env)->DeleteLocalRef(env, down_outline);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "down set");
//     }
// 
//     if (outline_class_found) {
//         (*env)->DeleteLocalRef(env, outline_class);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "local ref deleted");
//     }
// 
//     return joutline;
// }
// #endif


void append_chars(char **buf, size_t *buf_size, const char *new_chars, size_t new_chars_len) {
    /*
    __android_log_print(ANDROID_LOG_DEBUG,
            PDFVIEW_LOG_TAG,
            "appending chars %d chars, current buf len: %d, current buf size: %d",
            (int)new_chars_len,
            *buf != NULL ? (int)strlen(*buf) : -1,
            (int)*buf_size);
    */
    if (*buf == NULL) {
        *buf = (char*)malloc(256);
        (*buf)[0] = 0;
        *buf_size = 256;
    }

    size_t new_len = strlen(*buf) + new_chars_len; /* new_len is number of chars, new_len+1 is min buf size */
    if (*buf_size < (new_len + 1)) {
        size_t new_size = 0; 
        char *new_buf = NULL; 
        /*
        __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG,
                "need to resize buf");
        */
        new_size = (new_len + 3) * 1.5;
        new_buf = (char*)realloc(*buf, new_size);
        *buf_size = new_size;
        *buf = new_buf;
    }
    strlcat(*buf, new_chars, new_len+1);
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "new string after append chars: \"%s\"", *buf);
}


/**
 * Extract text from given pdf page.
 * Returns dynamically allocated string to be freed by caller or NULL.
 */
char* extract_text(pdf_t *pdf, int pageno) {
    fz_device *dev = NULL;
    fz_page *page = NULL;
    fz_text_sheet *text_sheet = NULL;
    fz_text_page *text_page = NULL;
    fz_rect pagebox;
    int block_no = 0;
    int line_no = 0;
    int span_no = 0;
    int char_no = 0;
    char runechars[128] = "";
    int runelen = 0;

    size_t text_buf_size = 0;
    char *text = NULL; /* utf-8 text */

    if (pdf == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "extract_text: pdf is NULL");
        return NULL;
    }

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "rendering page text");
    page = fz_load_page(pdf->doc, pageno);
    text_sheet = fz_new_text_sheet(pdf->ctx);
    pagebox = get_page_box(pdf, pageno);
    text_page = fz_new_text_page(pdf->ctx, pagebox);
    dev = fz_new_text_device(pdf->ctx, text_sheet, text_page);
    fz_run_page(pdf->doc, page, dev, fz_identity, NULL);
    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "done rendering page text");

    /* for now lets just flatten */
    for(block_no = 0; block_no < text_page->len; ++block_no) {
        fz_text_block *text_block = &(text_page->blocks[block_no]);
        for(line_no = 0; line_no < text_block->len; ++line_no) {
            fz_text_line *line = &(text_block->lines[line_no]);
            for(span_no = 0; span_no < line->len; ++span_no) {
                fz_text_span *span = &(line->spans[span_no]);
                for(char_no = 0; char_no < span->len; ++char_no) {
                    fz_text_char *text_char = &(span->text[char_no]);
                    runelen = fz_runetochar(runechars, text_char->c);
                    append_chars(&text, &text_buf_size, runechars, runelen);
                }
            }
            append_chars(&text, &text_buf_size, "\n", 1);
        }
    }

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "done extracting text");

    // fz_free_text_page(pdf->ctx, text_page);
    // fz_free_text_sheet(pdf->ctx, text_sheet);
    // fz_free_page(pdf->doc, page);
    // fz_free_device(dev);

    // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "extracted text, len: %d, chars: %s", text_len, text);
    return text;
}




/* vim: set sts=4 ts=4 sw=4 et: */

