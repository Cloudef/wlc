HOW TO CONTRIBUTE
-----------------

Look at the project issues list for things to do.
If you are planning contributing code that does not fix issue, visit the project IRC channel first.

Include clear description in comment, what your commit does.
If it fixes issue, include issue code in the description.

Eventually some testing guidelines will be added, but for now there is none.

If you have questions or need help, visit the project IRC channel.

MAKING CHANGES
--------------

- Fork the repository.
- Create branch for your feature.
- Follow the code style guidelines below and make changes.
- Open PR against master.
- When your PR is approved, squash your commits into one if they aren't already.
- PR gets merged.

CODE STYLE
----------

Project uses the C99 standard, avoid GNU and other unportable extensions.
Use C99 standard types. (stdbool, stdint)

3 spaces indentation, no tabs. Unix newlines (LF), UTF8 files and no BOM.

Variable declarations should be done where the variables are first used.
Do not put variable declarations top of the function like in ANSI C, this is pointless.

Asserts are encouraged, use them whenever it makes sense.
When validating function arguments (user input), put the asserts at top of the function.
This helps others to immediately see what is not valid input for the function.

Avoid useless comments in code. Code should be clear enough to understand without comments.
If something is hard to explain with comment, or you are not sure of something, the code probably could be simplified.
Comments are there when something has good reason to be explained.

Do not use typedef without a good reason. See Linux kernel's `Chapter 5 Typedefs <https://kernel.org/doc/Documentation/CodingStyle>`_

Use const whenever variable should not change.

Use static always when symbol should not be exposed outside, do not use static inside functions unless really needed.

Use newline after type name in function declarations. For function prototypes, just keep them one line.

Single line conditionals are allowed, however if the conditional contains else, it should be bracketed.

Avoid unnecessary whitespace and newlines.

Put opening brace on same line for anything except function declarations and cases.

Do not put spaces around parenthesis, with exception of open parenthesis for keyword (if, while, for)

Put spaces around arithmetic operators. (1+2 -> 1 + 2)

Put spaces after colons. (a,b,c -> a, b, c)

Get familiar with the utility functions included in `chck <https://github.com/Cloudef/chck>`_, especially the chck_string and chck_pool.
Any useful generic utility function should be contributed there.

.. code:: c

   #include <stdint.h>
   #include <stdbool.h>

   #if INDENT_CPP
   #  define EXPLAIN "Put # always leftmost and indent with spaces after it"
   #endif

   enum type {
      WHITE,
      GRAY,
      BLACK,
   };

   struct foo {
      bool bar;
      enum type type;
   };

   bool prototype(int32_t foo);

   /**
    * Function comment block.
    * Most editors do this formatting automatically.
    *
    * Always use static when the function is not supposed to be exposed outside.
    */
   static bool
   declaration(int32_t foo)
   {
      // User input assertation.
      // In this case we document developer, foo must be between -32 and 32.
      assert(foo > -32 && foo < 32);

      bool bar = false;

      // Single line ifs are allowed
      if (foo == 1)
         bar = true;

      // However if you must use else, use braces
      if ((foo + bar) * ~foo == 4) {
         foo = 8;
      } else {
         bar = false;
      }

      if (foo == 8)
         goto error;

      // Pointer operators (star, reference) should be next to variable.
      void *baf = NULL, *baz = NULL;

      return bar;

      // Labels are aligned to left
   error:
      return false;
   }


UNCRUSTIFY
----------

The repository contains `Uncrustify <https://github.com/bengardner/uncrustify>`_ configuration
for automatic styling of source code. While it does good job overall, there are few pitfalls.

The most common one is that it thinks anything with operators after cast is arithmetic.

.. code:: c

    // formats this
    static int foo = (bar)~0;

    // to this
    static int foo = (bar) ~0;
