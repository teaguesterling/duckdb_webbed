{ pkgs, lib, config, inputs, ... }:

{
  enterShell = ''
    export GEN=ninja
    export VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake)
  '';

  # https://devenv.sh/packages/
  packages = with pkgs; [
    git 
    gnumake

    # For faster compilation
    ninja

    # C/C++ tools
    autoconf
    automake

    # For this extension specifically
    libxml2
  ];

  # https://devenv.sh/languages/
  languages.cplusplus.enable = true;

  # Run clang-tidy and clang-format on commits
  git-hooks.hooks = {
    clang-format = {
      enable = true;
      types_or = [
        "c++"
        "c"
      ];
    };
    clang-tidy = {
      enable = false;
      types_or = [
        "c++"
        "c"
      ];
      entry = "clang-tidy -p build --fix";
    };

    # Custom hook to run `make test` before commit
    unit-tests = {
      enable = true;

      name = "Unit tests";

      entry = "make test";

      types_or = [
        "c++"
        "c"
      ];
    };
  };
}
