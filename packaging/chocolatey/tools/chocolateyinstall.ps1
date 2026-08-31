$ErrorActionPreference = 'Stop';
$toolsDir   = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64      = "https://github.com/riccivr/gitcrawl/releases/download/v1.0.0/gitcrawl-windows-amd64.zip"

$packageArgs = @{
  packageName   = 'gitcrawl'
  unzipLocation = $toolsDir
  url64bit      = $url64
}

Install-ChocolateyZipPackage @packageArgs
