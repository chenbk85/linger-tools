About linger-tools:
-------------------

The tools here were written to carry out cross-platform testing of the
SO_LINGER socket option. You will find the results of our tests in the
following blog posts:

>  [https://www.nybek.com/blog/2015/03/05/cross-platform-testing-of-so_linger/](https://www.nybek.com/blog/2015/03/05/cross-platform-testing-of-so_linger/)
>
>  [https://www.nybek.com/blog/2015/04/29/so_linger-on-non-blocking-sockets/](https://www.nybek.com/blog/2015/04/29/so_linger-on-non-blocking-sockets/)

Our primary motivation for releasing the code is so that others can
replicate the tests we carried out or test against new platforms.


About the licensing:
--------------------

The source code licensing here is a bit of a mess. The server tools,
linger-server.c and win-linger-server.c, are released under the LGPLv3,
and the client tool, linger-client.c, is released under the AGPLv3.

The code was developed for in-house research and in the process we
borrows snippets of code from Michael Kerrisk's book, The Linux
Programming Interface [2010]. In honouring the terms his code was
released under we ended up with the aforementioned jumble of
licenses.
