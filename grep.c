/*
 * Mini grep implementation for busybox using libc regex.
 *
 * Copyright (C) 1999,2000 by Lineo, inc.
 * Written by Mark Whitley <markw@lineo.com>, <markw@enol.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* for getopt() */
#include <regex.h>
#include <string.h> /* for strerror() */
#include <errno.h>
#include "internal.h"

extern int optind; /* in unistd.h */
extern int errno;  /* for use with strerror() */

static const char grep_usage[] =
	"grep [-ihHnqvs] pattern [files...]\n"
#ifndef BB_FEATURE_TRIVIAL_HELP
	"\nSearch for PATTERN in each FILE or standard input.\n\n"
	"OPTIONS:\n"
	"\t-H\tprefix output lines with filename where match was found\n"
	"\t-h\tsuppress the prefixing filename on output\n"
	"\t-i\tignore case distinctions\n"
	"\t-n\tprint line number with output lines\n"
	"\t-q\tbe quiet. Returns 0 if result was found, 1 otherwise\n"
	"\t-v\tselect non-matching lines\n"
	"\t-s\tsuppress file open/read error messages\n\n"
#endif
	;

static const int GROWBY = 80; /* how large we will grow strings by */

/* options */
static int ignore_case       = 0;
static int print_filename    = 0;
static int print_line_num    = 0;
static int be_quiet          = 0;
static int invert_search     = 0;
static int suppress_err_msgs = 0;

/* globals */
static regex_t regex; /* storage space for compiled regular expression */
static int nmatches = 0; /* keeps track of the number of matches */
static char *cur_file = NULL; /* the current file we are reading */


/* This returns a malloc'ed char * which must be stored and free'ed */
/* XXX: This function should probably go in a 'common'/'util'/'misc' file
 * somewhere so it can be used by other folks. */
static char *get_line_from_file(FILE *file)
{
	int ch;
	int idx = 0;
	char *linebuf = NULL;
	int linebufsz = 0;

	while (1) {
		ch = fgetc(file);
		if (ch == EOF)
			break;
		/* grow the line buffer as necessary */
		if (idx > linebufsz-1)
			linebuf = realloc(linebuf, linebufsz += GROWBY);
		linebuf[idx++] = (char)ch;
		if ((char)ch == '\n')
			break;
	}

	if (idx == 0)
		return NULL;

	linebuf[idx] = 0;
	return linebuf;
}

static void print_matched_line(char *line, int linenum)
{
	if (print_filename)
		printf("%s:", cur_file);
	if (print_line_num)
		printf("%i:", linenum);

	printf("%s", line);
}

static void grep_file(FILE *file)
{
	char *line = NULL;
	int ret;
	int linenum = 0;

	while ((line = get_line_from_file(file)) != NULL) {
		linenum++;
		ret = regexec(&regex, line, 0, NULL, 0);
		if (ret == 0 && !invert_search) { /* match */

			/* if we found a match but were told to be quiet, stop here and
			 * return success */
			if (be_quiet) {
				regfree(&regex);
				exit(0);
			}

			nmatches++;

			print_matched_line(line, linenum);

		} else if (ret == REG_NOMATCH && invert_search) {
			print_matched_line(line, linenum);
		}

		free(line);
	}
}

extern int grep_main(int argc, char **argv)
{
	int opt;
	int reflags;
	int ret;

	/* do special-case option parsing */
	if (argv[1] && (strcmp(argv[1], "--help") == 0))
		usage(grep_usage);

	/* do normal option parsing */
	while ((opt = getopt(argc, argv, "iHhnqvs")) > 0) {
		switch (opt) {
			case 'i':
				ignore_case++;
				break;
			case 'H':
				print_filename++;
				break;
			case 'h':
				print_filename--;
				break;
			case 'n':
				print_line_num++;
				break;
			case 'q':
				be_quiet++;
				break;
			case 'v':
				invert_search++;
				break;
			case 's':
				suppress_err_msgs++;
				break;
		}
	}

	/* argv[optind] should be the regex pattern; no pattern, no worky */
	if (argv[optind] == NULL)
		usage(grep_usage);

	/* compile the regular expression */
	reflags = REG_NOSUB; /* we're not going to mess with sub-expressions */
	if (ignore_case)
		reflags |= REG_ICASE;
	if ((ret = regcomp(&regex, argv[optind], reflags)) != 0) {
		int errmsgsz = regerror(ret, &regex, NULL, 0);
		char *errmsg = malloc(errmsgsz);
		if (errmsg == NULL) {
			fprintf(stderr, "grep: memory error\n");
			regfree(&regex);
			exit(1);
		}
		regerror(ret, &regex, errmsg, errmsgsz);
		fprintf(stderr, "grep: %s\n", errmsg);
		free(errmsg);
		regfree(&regex);
		exit(1);
	}

	/* argv[(optind+1)..(argc-1)] should be names of file to grep through. If
	 * there is more than one file to grep, we will print the filenames */
	if ((argc-1) - (optind+1) > 0)
		print_filename++;

	/* If no files were specified, take input from stdin. Otherwise, we grep
	 * through all the files specified. */
	if (argv[optind+1] == NULL) {
		grep_file(stdin);
	} else {
		int i;
		FILE *file;
		for (i = optind + 1; i < argc; i++) {
			cur_file = argv[i];
			file = fopen(cur_file, "r");
			if (file == NULL) {
				if (!suppress_err_msgs)
					fprintf(stderr, "grep: %s: %s\n", cur_file, strerror(errno));
			} else {
				grep_file(file);
				fclose(file);
			}
		}
	}

	regfree(&regex);

	if (nmatches == 0)
		return 1;

	return 0;
}
