<!--
SPDX-FileCopyrightText: 2024 Google Inc.
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# License

ng-log is distributed under the terms of the
[3-Clause BSD License](https://spdx.org/licenses/BSD-3-Clause.html).

ng-log continues the development of Google's glog. The code inherited from glog
remains copyrighted by Google Inc. and is licensed by Google under the 3-Clause
BSD License. All changes and additions made since the fork are copyrighted by
the ng-log contributors and are licensed under the same terms. ng-log is not
affiliated with or endorsed by Google.

The project follows the [REUSE](https://reuse.software/) specification. Every
source file states its copyright holders and license using
[SPDX](https://spdx.dev/) tags such as

<!-- REUSE-IgnoreStart -->
``` cpp
// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
```
<!-- REUSE-IgnoreEnd -->

Files that cannot carry such a header are covered by `REUSE.toml` in the
project's root directory. The full license texts are located in the `LICENSES/`
directory.

The following file is licensed differently. It is not part of the installed
library interface.

| File                   | License                                                 | Purpose                                                  |
| ---------------------- | ------------------------------------------------------- | -------------------------------------------------------- |
| `src/fuzz_demangle.cc` | [Apache-2.0](https://spdx.org/licenses/Apache-2.0.html) | Fuzz target for OSS-Fuzz that is not part of the library |

## The 3-Clause BSD License

--8<-- "LICENSE.md"
