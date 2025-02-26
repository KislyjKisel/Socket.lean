import Lake
open Lake DSL

package Socket {
  precompileModules := true
}

@[default_target]
lean_lib Socket

def cDir   := "native"
def ffiSrc := "native.c"
def ffiO   := "ffi.o"
def ffiLib := "ffi"

target ffi.o pkg : FilePath := do
  let oFile := pkg.buildDir / ffiO
  let srcJob ← inputFile <| pkg.dir / cDir / ffiSrc
  buildO ffiSrc oFile srcJob #["-I", (← getLeanIncludeDir).toString] #["-fPIC"]

extern_lib ffi pkg := do
  let name := nameToStaticLib ffiLib
  let ffiO ← fetch <| pkg.target ``ffi.o
  buildStaticLib (pkg.nativeLibDir / name) #[ffiO]

script examples do
  let examplesDir ← ("examples" : FilePath).readDir
  for ex in examplesDir do
    IO.println ex.path
    let o ← IO.Process.output {
      cmd := "lake"
      args := #["build"]
      cwd := ex.path
    }
    IO.println o.stderr
  return 0

script clean do
  let examplesDir ← ("examples" : FilePath).readDir
  let _ ← IO.Process.output {
      cmd := "lake"
      args := #["clean"]
  }
  for ex in examplesDir do
    let _ ← IO.Process.output {
      cmd := "lake"
      args := #["clean"]
      cwd := ex.path
    }
  return 0
