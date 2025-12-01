/**
 * @file error.h
 * @brief Error handling and reporting utilities.
 *
 * Provides simple helper functions for standardized error reporting.
 * This includes setting the program name (for message prefixes) and
 * printing formatted error messages to stderr.
 */
 
#ifndef ERROR_H
#define ERROR_H

/**
 * @brief Set the program name for error reporting.
 *
 * @param argv0 The first argument from main() (program name).
 */
void error_set_name(const char *argv0);

/**
 * @brief Prints an error message to stderr.
 *
 * Formats and prints an error message in the form:
 *     program_name: function: message: strerror(err)
 *
 * Omits the strerror part if err == 0, or both the
 * func and strerror if func == NULL.
 *
 * @param func  Name of the function reporting the error (e.g., __func__),
 *              or NULL if not applicable.
 * @param msg   Description of the error.
 * @param err   Error code (e.g., errno), or 0 if not applicable.
 */
void error_print(const char *func, const char *msg, const int err);

#endif /* ERROR_H */
