class Gitcrawl < Formula
  desc "Git-native content-addressable web archival engine and crawler"
  homepage "https://github.com/riccivr/gitcrawl"
  url "https://github.com/riccivr/gitcrawl/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "MIT"

  depends_on "git"
  depends_on "curl"

  def install
    system "make"
    bin.install "gitcrawl"
    man1.install "gitcrawl.1"
  end

  test do
    system "#{bin}/gitcrawl", "version"
  end
end
