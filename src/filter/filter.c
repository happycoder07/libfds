#include <stdbool.h>
#include <libfds.h>
#include "filter.h"
#include "parser.h"
#include "scanner.h"

// TODO: clean this up, this is for debugging purposes for now
typedef void *yyscan_t;
extern int yydebug;

fds_filter_t *
fds_filter_create()
{
    fds_filter_t *filter = malloc(sizeof(fds_filter_t));
    if (filter == NULL) {
        return NULL;
    }
    filter->lookup_callback = NULL;
    filter->const_callback = NULL;
    filter->field_callback = NULL;
    filter->ast = NULL;
    filter->eval_tree = NULL;
    filter->reset_context = false;
    filter->user_context = NULL;
    filter->data = NULL;
    filter->error_count = 0;
    filter->errors = NULL;
    return filter;
}

void
fds_filter_destroy(fds_filter_t *filter)
{
    ast_destroy(filter->ast);
    eval_tree_destroy(filter->eval_tree);
    free(filter);
}

void
fds_filter_set_lookup_callback(fds_filter_t *filter, fds_filter_lookup_callback_t lookup_callback)
{
    assert(filter != NULL);
    filter->lookup_callback = lookup_callback;
}

void
fds_filter_set_const_callback(fds_filter_t *filter, fds_filter_const_callback_t const_callback)
{
    assert(filter != NULL);
    filter->const_callback = const_callback;
}

void
fds_filter_set_field_callback(fds_filter_t *filter, fds_filter_field_callback_t field_callback)
{
    assert(filter != NULL);
    filter->field_callback = field_callback;
}

void
fds_filter_set_user_context(fds_filter_t *filter, void *user_context)
{
    assert(filter != NULL);
    filter->user_context = user_context;
}

void *
fds_filter_get_user_context(fds_filter_t *filter)
{
    assert(filter != NULL);
    return filter->user_context;
}

int
fds_filter_compile(fds_filter_t *filter, const char *filter_expression)
{
    assert(filter != NULL);
    assert(filter->lookup_callback != NULL);

    // TODO: clean this mess up
    yyscan_t scanner;
    yydebug = 0;
    yylex_init(&scanner);
    pdebug("Parsing %s", filter_expression);
    YY_BUFFER_STATE buffer = yy_scan_string(filter_expression, scanner);
    yyparse(filter, scanner);
    pdebug("Finished parsing");
    yy_delete_buffer(buffer, scanner);
    yylex_destroy(scanner);

    if (fds_filter_get_error_count(filter) != 0) {
        pdebug("Parsing failed");
        return FDS_FILTER_FAIL;
    }
    pdebug("Filter compiled - input: %s", filter_expression);
    ast_print(stderr, filter->ast);

    int return_code;

    return_code = preprocess(filter);
    if (return_code != FDS_FILTER_OK) {
        pdebug("ERROR: Preprocess failed!");
        return return_code;
    }

    return_code = semantic_analysis(filter);
    if (return_code != FDS_FILTER_OK) {
        pdebug("ERROR: Semantic analysis failed!");
        return return_code;
    }

    return_code = optimize(filter);
    if (return_code != FDS_FILTER_OK) {
        pdebug("ERROR: Optimize failed!");
        return return_code;
    }

    pdebug("====== Final AST ======");
    ast_print(stderr, filter->ast);
    pdebug("=======================");

    struct eval_node *eval_tree = eval_tree_generate(filter, filter->ast);
    if (eval_tree == NULL) {
        pdebug("Generate eval tree from AST failed");
        return FDS_FILTER_FAIL;
    }
    eval_tree_print(stderr, eval_tree);
    filter->eval_tree = eval_tree;

    return FDS_FILTER_OK;
}

int
fds_filter_evaluate(fds_filter_t *filter, void *input_data)
{
    assert(filter != NULL);
    assert(filter->lookup_callback != NULL);
    assert(filter->const_callback != NULL);
    assert(filter->field_callback != NULL);
    assert(filter->ast != NULL);
    assert(filter->eval_tree != NULL);

    filter->data = input_data;

    int return_code = eval_tree_evaluate(filter, filter->eval_tree);
    if (return_code == FDS_OK) {
        return filter->eval_tree->value.bool_ ? FDS_FILTER_YES : FDS_FILTER_NO;
    } else {
        return FDS_FILTER_FAIL;
    }
}

int
fds_filter_get_error_count(fds_filter_t *filter)
{
    return filter->error_count;
}

const char *
fds_filter_get_error_message(fds_filter_t *filter, int index)
{
    if (index >= filter->error_count) {
        return NULL;
    }
    return filter->errors[index].message;
}

int
fds_filter_get_error_location(fds_filter_t *filter, int index, struct fds_filter_location *location)
{
    if (index >= filter->error_count) {
        return 0;
    }
    if (filter->errors[index].location.first_line == -1) {
        return 0;
    }
    *location = filter->errors[index].location;
    return 1;
}

const struct fds_filter_ast_node *
fds_filter_get_ast(fds_filter_t *filter)
{
    return filter->ast;
}

FDS_API int
fds_filter_print_errors(fds_filter_t *filter, FILE *out_stream)
{
    int i;
    pdebug("Error count: %d", fds_filter_get_error_count(filter));
    for (i = 0; i < fds_filter_get_error_count(filter); i++) {
        struct fds_filter_location location;
        const char *error_message = fds_filter_get_error_message(filter, i);
        fprintf(out_stream, "ERROR: %s", error_message);
        if (fds_filter_get_error_location(filter, i, &location)) {
            fprintf(out_stream, " on line %d:%d, column %d:%d",
                    location.first_line, location.last_line, location.first_column, location.last_column);
        }
        fprintf(out_stream, "\n");
    }
    return i;
}

