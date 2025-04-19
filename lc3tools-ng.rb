# Formula for building lc3tools-ng on Macs in the future (NOT TESTED!)
class Lc3toolsNg < Formula
  desc "Asssembler and simulator for the LC-3 virtual computer"
  homepage "https://github.com/LandonTheCoder/lc3tools-ng"
  # URL to source tarball or git repository
  url "https://github.com/LandonTheCoder/lc3tools-ng.git", branch: "main"
  # Used to verify source tarballs
#  sha256 ""
  license "GPL-2.0-only"
  # Required to build
  version "13.0~beta"


  # Dependencies
  depends_on "tcl-tk@8"
  # Works without it, just is nice to have.
  depends_on "readline" => :recommended
  # Build dependencies
  depends_on "flex" => :build
  depends_on "meson" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build
  # How do I declare for xxd (comes from vim)?
  # Alternatively, find a program to generate the headers itself.

  # How to actually build it.
  def install
    # Homebrew provides pre-defined arguments for Meson builds
    system "meson", "setup", "build", "-Dhardcode_wish_path=true", *std_meson_args
    system "meson", "compile", "-C", "build", "--verbose"
    system "meson", "install", "-C", "build"
  end
end


