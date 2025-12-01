/**
 * @file error.c
 * @brief Implementation of error handling utilities.
 */

#include <stdio.h>
#include <string.h>

#include "error.h"

/**
 * @brief Global variable storing the program name for error messages.
 *
 * This variable should be initialized by calling error_set_name()
 * before any calls to error_print().
 */
const char *program_name = "tinyshell";

/**
 * @copydoc set_program_name()
 */
void
error_set_name(const char *name)
{
	if (name) {
		const char *slash = strrchr(name, '/');
		program_name = slash ? slash + 1 : name;
	}
}

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
void
error_print(const char *func, const char *msg, const int err)
{
	if (!func) {
		fprintf(stderr, "%s: %s\n", program_name, msg);
		return;
	}

	if (!err) {
		fprintf(stderr, "%s: %s: %s\n", program_name, func, msg);
		return;
	}

	fprintf(stderr, "%s: %s: %s: %s\n", program_name, func, msg, strerror(err));
}
