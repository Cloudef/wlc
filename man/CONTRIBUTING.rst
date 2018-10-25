CONTRIBUTING DOCUMENTATION
==========================
These manuals use the groff markup using the man macros, see `groff(7)
<http://man7.org/linux/man-pages/man7/groff.7.html>`_, `groff_man(7)
<http://man7.org/linux/man-pages/man7/groff_man.7.html>`_ and `man(7)
<http://man7.org/linux/man-pages/man7/man.7.html>`_ for detailed information.

This project closely follows the guidelines specified in
`man-pages(7) <http://man7.org/linux/man-pages/man7/man-pages.7.html>`_ so
please try to read through them.

Avoid in-line escapes
---------------------
Instead of using ``\fB`` to start boldface and ``\fR`` to switch back to roman
such as:

.. code:: groff

    foo \fBbar\fR baz

prefer

.. code:: groff

    foo
    .B bar
    baz

Section headers
---------------
Section content should come directly after the ``.SH`` section header
definition:

.. code:: groff

    .SH NAME
    foo \- bar baz

Leave a linebreak between section headers.

.. code:: groff

    foo

    .SH EXAMPLE
    bar baz

Useful macros
-------------
Use the alternating form macros where it makes sense, such as ``.BR`` for
alternating between bold and roman or ``.IR`` for alternative between italics
and roman.  Note that one can use empty spaces ``" "`` to consume an
alternation.

.. code:: groff

    .SH DESCRIPTION
    .BR foo ()
    is a function which takes arguments
    .IR a ,
    .IR b ,
    and
    .IR c .
