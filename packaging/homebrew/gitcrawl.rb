class Gitcrawl < Formula
  desc "Git-native content-addressable web archival engine and crawler"
  homepage "https://github.com/riccivr/gitcrawl"
  url "https://github.com/riccivr/gitcrawl/archive/refs/tags/v1.1.0.tar.gz"
  sha256 "06abc73d76120d42dfaf8022b1314f1b5eb9710eefab89a41d3bff78cca9b6d4"
  license "MIT"

  depends_on "git"
  depends_on "curl"

  def install
    system "make"
    bin.install "gitcrawl"
    man1.install "gitcrawl.1"
  end

  test do
    system "#{bin}/gitcrawl", "-v"
  end
end
