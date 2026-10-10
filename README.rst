.. SPDX-FileCopyrightText: 2020-2024 Google Inc.
.. SPDX-FileCopyrightText: 2026 The ng-log contributors
..
.. SPDX-License-Identifier: BSD-3-Clause

ng-log
======

|Linux Github actions| |Windows Github actions| |macOS Github actions| |Codecov|

ng-log is a C++14 library that implements application-level logging. The library
provides logging APIs based on C++-style streams and various helper macros.

ng-log is a community maintained successor to Google's glog that is unrelated to
Google or its employees. For a Google maintained logging library please refer to
abseil-cpp.

Getting Started
---------------

Please refer to project's `documentation <https://ng-log.github.io/ng-log/>`_.

License
-------

ng-log is licensed under the 3-Clause BSD License. Code inherited from glog is
copyrighted by Google Inc. while all subsequent changes are copyrighted by the
ng-log contributors. The project is `REUSE <https://reuse.software/>`_ compliant
with each file stating its copyright and license using SPDX tags. Refer to the
`license documentation <docs/license.md>`_ for
details including the few files licensed differently.


.. |Linux Github actions| image:: https://github.com/ng-log/ng-log/actions/workflows/linux.yml/badge.svg
   :target: https://github.com/ng-log/ng-log/actions
.. |Windows Github actions| image:: https://github.com/ng-log/ng-log/actions/workflows/windows.yml/badge.svg
   :target: https://github.com/ng-log/ng-log/actions
.. |macOS Github actions| image:: https://github.com/ng-log/ng-log/actions/workflows/macos.yml/badge.svg
   :target: https://github.com/ng-log/ng-log/actions
.. |Codecov| image:: https://codecov.io/gh/ng-log/ng-log/branch/master/graph/badge.svg?token=7R5D3HA36B
   :target: https://codecov.io/gh/ng-log/ng-log
