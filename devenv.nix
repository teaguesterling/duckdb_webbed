{ pkgs, lib, config, inputs, ... }:

{
  # Give dotenv.resolved a value even though the dotenv integration is off.
  #
  # devenv.yaml tracks devenv-nixpkgs/rolling while devenv.lock pins the devenv
  # MODULES at 78e4cb0a (2025-10-07, "next release is 1.10.1"), and CI installs the
  # CLI with an unpinned `nix profile install nixpkgs#devenv` -- which now ships
  # 2.4.0. That mismatch broke every `devenv shell` in CI with:
  #
  #   × Failed to evaluate dotenv configuration
  #     The option `dotenv.resolved' was accessed but has no value defined.
  #
  # At the locked revision `dotenv.resolved` is declared with NO default and is only
  # assigned inside `lib.mkIf cfg.enable`; this repo never enables dotenv, so the
  # option stays valueless. A 2.x CLI reads config.dotenv.resolved unconditionally
  # and dies on it. Upstream fixed the same incompatibility from their side in
  # cachix/devenv 00832edc ("fix(dotenv): support older CLIs", 2026-08-31) and now
  # declares the option with `default = { }`.
  #
  # Setting it here is the smallest fix and is forward-safe: the option merges rather
  # than conflicts (nothing else defines it while dotenv is disabled), and `{ }`
  # satisfies both the locked type (attrsOf anything) and the current one
  # (attrsOf str). Once devenv.lock is refreshed past 00832edc this line becomes
  # redundant -- not wrong -- and can go.
  #
  # The durable fix is `devenv update`, which needs nix locally; this unblocks CI
  # meanwhile without pinning the CLI back to an unreleased revision (there is no
  # v1.10.1 tag: the locked commit sits between v1.10 and v1.11).
  dotenv.resolved = { };

  enterShell = ''
    export GEN=ninja
    export VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake
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
