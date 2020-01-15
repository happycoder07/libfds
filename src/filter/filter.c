#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfds.h>

#include "filter.h"
#include "ast_common.h"
#include "eval_common.h"
#include "opts.h"
#include "error.h"
#include "opts.h"
#include "parser.h"
#include "scanner.h"

// fds_filter_create(fds_filter_t **filter, const char *expr, fds_filter_opts_t *opts)
int
fds_filter_create(const char *expr, const fds_filter_opts_t *opts, fds_filter_t **out_filter)
{
    *out_filter = calloc(1, sizeof(fds_filter_t));
    if (!*out_filter) {
        return FDS_ERR_NOMEM;
    }

    struct scanner_s scanner;
    init_scanner(&scanner, expr);

    printf("----- parse -----\n");
    (*out_filter)->error = parse_filter(&scanner, &(*out_filter)->ast);
    if ((*out_filter)->error != NO_ERROR) {
        destroy_ast((*out_filter)->ast);
        (*out_filter)->ast = NULL;
        return (*out_filter)->error->code;
    }
    print_ast(stdout, (*out_filter)->ast);

    printf("----- semantic -----\n");
    (*out_filter)->error = resolve_types((*out_filter)->ast, opts);
    if ((*out_filter)->error != NO_ERROR) {
        destroy_ast((*out_filter)->ast);
        (*out_filter)->ast = NULL;
        return (*out_filter)->error->code;
    }
    print_ast(stdout, (*out_filter)->ast);

    printf("----- generator -----\n");
    (*out_filter)->error = generate_eval_tree((*out_filter)->ast, opts, &(*out_filter)->eval_root);
    if ((*out_filter)->error != NO_ERROR) {
        destroy_ast((*out_filter)->ast);
        (*out_filter)->ast = NULL;
        return (*out_filter)->error->code;
    }
    (*out_filter)->eval_runtime.data_cb = opts->data_cb;
    (*out_filter)->eval_runtime.user_ctx = opts->user_ctx;
    print_eval_tree(stdout, (*out_filter)->eval_root);

    return FDS_OK;
}

bool
fds_filter_eval(fds_filter_t *filter, void *data)
{
    printf("----- evaluate -----\n");
    filter->eval_runtime.data = data;
    evaluate_eval_tree(filter->eval_root, &filter->eval_runtime);
    print_eval_tree(stdout, filter->eval_root);
    return filter->eval_root->value.b;
}

void
fds_filter_destroy(fds_filter_t *filter)
{
    if (!filter) {
        return;
    }
    destroy_eval_tree(filter->eval_root);
    destroy_ast(filter->ast);
    destroy_error(filter->error);
    free(filter);
}

error_t
fds_filter_get_error(fds_filter_t *filter)
{
    if (!filter) {
        return MEMORY_ERROR;
    }
    return filter->error;
}