# Installation using Package Managers

## vcpkg

You can download and install ng-log using the
[vcpkg](https://github.com/Microsoft/vcpkg) dependency manager:

``` bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg integrate install
./vcpkg install ng-log
```

The ng-log port in vcpkg is kept up to date by Microsoft team members and
community contributors. If the version is out of date, please create an
issue or pull request on the vcpkg repository.
