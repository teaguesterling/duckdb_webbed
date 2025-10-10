# DuckDB webbed - DuckDB extension to interact with xml Query extension
You can read more about the purpose from [README.md](./README.md).

## Building extension on MacOS
Install requirements:
```
$ git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
```

Ensure that git submodules have been checkout:
```
$ git submodule update --init --remote --recursive
```

Build the project using ninja and vckpg:
```
$ make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake
```

## Caching
Don't remove the `build` directory everytime after changes. This will make it much more slower to iterate.

You're only allowed to delete the `build` if you think it's a "systemic issue with the development environment".

## Reading the code of the core DuckDB
DuckDB core is a git subdmodule of the project in `duckdb/` folder. If you need to search for examples don't do Web searches but instead check for code inside that folder.

## Testing
**IMPORTANT: After the build is finished you need to run: `make test`**

This will run tests in the `tests/` folder.

## Fixing issues from Github
When you're fixing a issue from Github commit your changes after you finish. If the git commit hooks return any errors fix them before finishing with the task.

**IMPORTANT: You're not allowed to use `git commit --no-verify` ever**