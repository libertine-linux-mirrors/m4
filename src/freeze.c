/* GNU m4 -- A simple macro processor

   Copyright (C) 1989, 1990, 1991, 1992, 1993, 1994, 2006, 2007
   Free Software Foundation, Inc.

   This file is part of GNU M4.

   GNU M4 is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   GNU M4 is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* This module handles frozen files.  */

#include "m4.h"

/*-------------------------------------------------------------------.
| Destructively reverse a symbol list and return the reversed list.  |
`-------------------------------------------------------------------*/

static symbol *
reverse_symbol_list (symbol *sym)
{
  symbol *result;
  symbol *next;

  result = NULL;
  while (sym)
    {
      next = SYMBOL_NEXT (sym);
      SYMBOL_NEXT (sym) = result;
      result = sym;
      sym = next;
    }
  return result;
}

/*------------------------------------------------.
| Produce a frozen state to the given file NAME.  |
`------------------------------------------------*/

void
produce_frozen_state (const char *name)
{
  FILE *file;
  int h;
  symbol *sym;
  const builtin *bp;

  file = fopen (name, O_BINARY ? "wb" : "w");
  if (!file)
    {
      m4_error (0, errno, NULL, _("cannot open %s"), name);
      return;
    }

  /* Write a recognizable header.  */

  xfprintf (file, "# This is a frozen state file generated by %s\n",
	   PACKAGE_STRING);
  xfprintf (file, "V1\n");

  /* Dump quote delimiters.  */

  if (strcmp (lquote.string, DEF_LQUOTE) || strcmp (rquote.string, DEF_RQUOTE))
    {
      xfprintf (file, "Q%d,%d\n", (int) lquote.length, (int) rquote.length);
      fputs (lquote.string, file);
      fputs (rquote.string, file);
      fputc ('\n', file);
    }

  /* Dump comment delimiters.  */

  if (strcmp (bcomm.string, DEF_BCOMM) || strcmp (ecomm.string, DEF_ECOMM))
    {
      xfprintf (file, "C%d,%d\n", (int) bcomm.length, (int) ecomm.length);
      fputs (bcomm.string, file);
      fputs (ecomm.string, file);
      fputc ('\n', file);
    }

  /* Dump all symbols.  */

  for (h = 0; h < hash_table_size; h++)
    {

      /* Process all entries in one bucket, from the last to the first.
	 This order ensures that, at reload time, pushdef's will be
	 executed with the oldest definitions first.  */

      symtab[h] = reverse_symbol_list (symtab[h]);
      for (sym = symtab[h]; sym; sym = SYMBOL_NEXT (sym))
	{
	  switch (SYMBOL_TYPE (sym))
	    {
	    case TOKEN_TEXT:
	      xfprintf (file, "T%d,%d\n",
			(int) strlen (SYMBOL_NAME (sym)),
			(int) strlen (SYMBOL_TEXT (sym)));
	      fputs (SYMBOL_NAME (sym), file);
	      fputs (SYMBOL_TEXT (sym), file);
	      fputc ('\n', file);
	      break;

	    case TOKEN_FUNC:
	      bp = find_builtin_by_addr (SYMBOL_FUNC (sym));
	      if (bp == NULL)
		{
		  assert (!"produce_frozen_state");
		  abort ();
		}
	      xfprintf (file, "F%d,%d\n",
			(int) strlen (SYMBOL_NAME (sym)),
			(int) strlen (bp->name));
	      fputs (SYMBOL_NAME (sym), file);
	      fputs (bp->name, file);
	      fputc ('\n', file);
	      break;

	    case TOKEN_VOID:
	      /* Ignore placeholder tokens that exist due to traceon.  */
	      break;

	    default:
	      assert (!"produce_frozen_state");
	      abort ();
	      break;
	    }
	}

      /* Reverse the bucket once more, putting it back as it was.  */

      symtab[h] = reverse_symbol_list (symtab[h]);
    }

  /* Let diversions be issued from output.c module, its cleaner to have this
     piece of code there.  */

  freeze_diversions (file);

  /* All done.  */

  fputs ("# End of frozen state file\n", file);
  if (close_stream (file) != 0)
    m4_error (EXIT_FAILURE, errno, NULL, _("unable to create frozen state"));
}

/*----------------------------------------------------------------------.
| Issue a message saying that some character is an EXPECTED character.  |
`----------------------------------------------------------------------*/

static void
issue_expect_message (int expected)
{
  if (expected == '\n')
    m4_error (EXIT_FAILURE, 0, NULL, _("expecting line feed in frozen file"));
  else
    m4_error (EXIT_FAILURE, 0, NULL,
	      _("expecting character `%c' in frozen file"), expected);
}

/*-------------------------------------------------.
| Reload a frozen state from the given file NAME.  |
`-------------------------------------------------*/

/* We are seeking speed, here.  */

void
reload_frozen_state (const char *name)
{
  FILE *file;
  int character;
  int operation;
  char *string[2];
  int allocated[2];
  int number[2];
  const builtin *bp;

#define GET_CHARACTER \
  (character = getc (file))

#define GET_NUMBER(Number) \
  do								\
    {								\
      (Number) = 0;						\
      while (isdigit (character))				\
	{							\
	  (Number) = 10 * (Number) + character - '0';		\
	  GET_CHARACTER;					\
	}							\
    }								\
  while (0)

#define VALIDATE(Expected) \
  do								\
    {								\
      if (character != (Expected))				\
	issue_expect_message ((Expected));			\
    }								\
  while (0)

  /* Skip comments (`#' at beginning of line) and blank lines, setting
     character to the next directive or to EOF.  */

#define GET_DIRECTIVE \
  do								\
    {								\
      GET_CHARACTER;						\
      if (character == '#')					\
	{							\
	  while (character != EOF && character != '\n')		\
	    GET_CHARACTER;					\
	  VALIDATE ('\n');					\
	}							\
    }                                                           \
  while (character == '\n')

  file = m4_path_search (name, NULL);
  if (file == NULL)
    m4_error (EXIT_FAILURE, errno, NULL, _("cannot open %s"), name);

  allocated[0] = 100;
  string[0] = xcharalloc ((size_t) allocated[0]);
  allocated[1] = 100;
  string[1] = xcharalloc ((size_t) allocated[1]);

  /* Validate format version.  Only `1' is acceptable for now.  */
  GET_DIRECTIVE;
  VALIDATE ('V');
  GET_CHARACTER;
  GET_NUMBER (number[0]);
  if (number[0] > 1)
    m4_error (EXIT_MISMATCH, 0, NULL,
	      _("frozen file version %d greater than max supported of 1"),
	      number[0]);
  else if (number[0] < 1)
    m4_error (EXIT_FAILURE, 0, NULL,
	      _("ill-formed frozen file, version directive expected"));
  VALIDATE ('\n');

  GET_DIRECTIVE;
  while (character != EOF)
    {
      switch (character)
	{
	default:
	  m4_error (EXIT_FAILURE, 0, NULL, _("ill-formed frozen file"));

	case 'C':
	case 'D':
	case 'F':
	case 'T':
	case 'Q':
	  operation = character;
	  GET_CHARACTER;

	  /* Get string lengths.  Accept a negative diversion number.  */

	  if (operation == 'D' && character == '-')
	    {
	      GET_CHARACTER;
	      GET_NUMBER (number[0]);
	      number[0] = -number[0];
	    }
	  else
	    GET_NUMBER (number[0]);
	  VALIDATE (',');
	  GET_CHARACTER;
	  GET_NUMBER (number[1]);
	  VALIDATE ('\n');

	  if (operation != 'D')
	    {

	      /* Get first string contents.  */

	      if (number[0] + 1 > allocated[0])
		{
		  free (string[0]);
		  allocated[0] = number[0] + 1;
		  string[0] = xcharalloc ((size_t) allocated[0]);
		}

	      if (number[0] > 0)
		if (!fread (string[0], (size_t) number[0], 1, file))
		  m4_error (EXIT_FAILURE, 0, NULL,
			    _("premature end of frozen file"));

	      string[0][number[0]] = '\0';
	    }

	  /* Get second string contents.  */

	  if (number[1] + 1 > allocated[1])
	    {
	      free (string[1]);
	      allocated[1] = number[1] + 1;
	      string[1] = xcharalloc ((size_t) allocated[1]);
	    }

	  if (number[1] > 0)
	    if (!fread (string[1], (size_t) number[1], 1, file))
	      m4_error (EXIT_FAILURE, 0, NULL,
			_("premature end of frozen file"));

	  string[1][number[1]] = '\0';
	  GET_CHARACTER;
	  VALIDATE ('\n');

	  /* Act according to operation letter.  */

	  switch (operation)
	    {
	    case 'C':

	      /* Change comment strings.  */

	      set_comment (string[0], string[1]);
	      break;

	    case 'D':

	      /* Select a diversion and add a string to it.  */

	      make_diversion (number[0]);
	      if (number[1] > 0)
		output_text (string[1], number[1]);
	      break;

	    case 'F':

	      /* Enter a macro having a builtin function as a definition.  */

	      bp = find_builtin_by_name (string[1]);
	      define_builtin (string[0], bp, SYMBOL_PUSHDEF);
	      break;

	    case 'T':

	      /* Enter a macro having an expansion text as a definition.  */

	      define_user_macro (string[0], number[0], string[1],
				 SYMBOL_PUSHDEF);
	      break;

	    case 'Q':

	      /* Change quote strings.  */

	      set_quotes (string[0], string[1]);
	      break;

	    default:

	      /* Cannot happen.  */

	      break;
	    }
	  break;

	}
      GET_DIRECTIVE;
    }

  free (string[0]);
  free (string[1]);
  errno = 0;
  if (ferror (file) || fclose (file) != 0)
    m4_error (EXIT_FAILURE, errno, NULL, _("unable to read frozen state"));

#undef GET_CHARACTER
#undef GET_DIRECTIVE
#undef GET_NUMBER
#undef VALIDATE
}
