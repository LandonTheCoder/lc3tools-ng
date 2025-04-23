# Formula for building lc3tools-ng on Macs
# This is not well-tested.
class Lc3toolsNg < Formula
  desc "Asssembler and simulator for the LC-3 virtual computer"
  homepage "https://github.com/LandonTheCoder/lc3tools-ng"
  # URL to source tarball or git repository
  # For git repository, it must specify branch, and possibly tag (for
  # building a specific version).
  url "https://github.com/LandonTheCoder/lc3tools-ng.git", branch: "main"
  # Used to verify source tarballs
#  sha256 ""
  license "GPL-2.0-only"
  # Version number (required to build)
  version "13.0~beta"

  # Dependencies
  # Needed for lc3sim-tk (although it's buggy)
  depends_on "tcl-tk@8"
  # Works without it, just is nice to have.
  depends_on "readline" => :recommended
  # Build dependencies
  depends_on "flex" => :build
  depends_on "meson" => :build
  depends_on "ninja" => :build
  # Used for detecting support libraries (readline)
  depends_on "pkgconf" => :build
  # Note: headers are generated through a built-in program here.

  # How to actually build it.
  def install
    # Homebrew provides pre-defined arguments for Meson builds
    # The hardcode_wish_path option makes the lc3sim-tk use an absolute path to wish,
    # instead of searching the PATH (like if using non-default tk). This makes sure
    # it runs against Homebrew's tk instead of the ancient version in macOS.
    system "meson", "setup", "build", "-Dhardcode_wish_path=true", *std_meson_args
    system "meson", "compile", "-C", "build", "--verbose"
    system "meson", "install", "-C", "build"
  end
end


