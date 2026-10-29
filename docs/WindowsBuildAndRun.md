# iiCameraIntegration: Windows 빌드 및 실행

카메라 메타데이터·제어와 미디어 형식 인식을 제공하는 C++23 SDK이다. 패키지와 네임스페이스는 Integration 표기를 사용한다.

검증 기록 갱신: `2026-10-20T02:45:37+09:00`. 변경 전 소스 기준 commit: `6d69f91f6330108f5c92a34a7afbbcd18ff851f8`. 아래 결과는 이번 변경을 포함한 작업 트리의 검증이다.

## 현재 검증 상태

아래 26개 검사 통과 기록은 2026-10-20 소스에 대한 이전 결과다. 2026-10-28 파일 포트 분리 이후 Windows 빌드와 실행은 아직 검증하지 않았다.

## 이 프로젝트의 의존성과 실행 범위

카메라 SDK 본체는 다른 SDK를 필요로 하지 않는다. 다음 모듈은 별도로 구성하는 소비자에서 사용한다.

- [iiFileProvider](../../../SDK/iiFileProvider/docs/WindowsBuildAndRun.md) — 별도 Media 소비자의 파일 생성·게시
- [iiGraphicAdjust](../../../SDK/iiGraphicAdjust/docs/WindowsBuildAndRun.md) — 별도 ImageProcessing 소비자에서 사용

- 본체에는 Windows 네이티브 카메라 API, pkg-config 및 FFmpeg 개발 헤더·라이브러리가 필요하다. iiFileProvider Native는 Media 소비자에만 필요하다.
- `Tools/FFmpeg/SDK9`의 LGPL shared FFmpeg SDK는 MSVC용 import library를 갖춘 설치본이다. 테스트 영상 생성은 libx264를 제공하는 `Tools/FFmpeg/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe`를 사용한다.
- iiGraphicAdjust는 별도로 구성하는 ImageProcessing 소비자에만 필요하다. 아래 구성은 카메라 SDK 본체이다.

프로젝트별 Windows 조건은 다음과 같다.

- CMake가 JSON fixture의 stdout을 수집할 때 UTF-8을 명시한다. 한글·emoji·따옴표·개행이 포함된 장치명을 검사하여 Windows ANSI code page에 따른 문자열 손상을 방지한다.
- 카메라 장치가 없으면 열거 결과가 비어 있을 수 있다. 실제 촬영·권한·드라이버 동작은 해당 장치에서 별도 확인한다.

## 환경 준비

PowerShell에서 이 프로젝트 루트로 이동한 뒤 순서대로 실행한다. workspace 배치는 `Workspace/SDK/<이름>`을 전제로 한다.

검증 도구는 MSVC 19.51 x64, Windows SDK 10.0.28000, CMake 4.4.4, Ninja 1.13.2이다. 개발 shell은 빌드 재현을 위해 Python 3.14.8을 선택하며 시스템 기본 Python 3.15.0과 구분한다.

```powershell
$ErrorActionPreference = 'Stop'
$project = (Get-Location).Path
$workspace = (Resolve-Path -LiteralPath '../..').Path
. (Join-Path $workspace 'SDK/Windows/Enter-DevEnvironment.ps1')
Set-Location -LiteralPath $project
$python = (Join-Path $workspace 'Tools/Python314/tools/python.exe').Replace('\', '/')
$compiler = (Get-Command cl.exe -ErrorAction Stop).Source.Replace('\', '/')
$toolchain = (Join-Path $workspace 'Tools/vcpkg/scripts/buildsystems/vcpkg.cmake').Replace('\', '/')
$msvcInclude = (Join-Path $workspace 'SDK/Windows/WindowsMsvcPrefix.cmake').Replace('\', '/')
$prefix = (Join-Path $env:USERPROFILE '.local/SDK/iiCameraIntegration').Replace('\', '/')
```

`Enter-DevEnvironment.ps1`은 Qt MSVC kit, vcpkg, 설치된 SDK의 검색 경로와 UTF-8 설정을 준비한다. `CMAKE_PREFIX_PATH`의 디렉터리 존재만으로 의존성 검증을 대신하지 않는다. 선행 SDK의 실행 검사와 설치를 먼저 완료해야 한다.

## 구성과 빌드

```powershell
$configure = @(
    '-S', '.', '-B', 'build', '-G', 'Ninja'
    '-DCMAKE_BUILD_TYPE=Release'
    "-DCMAKE_C_COMPILER=$compiler"
    "-DCMAKE_CXX_COMPILER=$compiler"
    "-DCMAKE_INSTALL_PREFIX=$prefix"
    "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH"
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    '-DVCPKG_TARGET_TRIPLET=x64-windows'
    "-DCMAKE_PROJECT_INCLUDE=$msvcInclude"
    "-DPython3_EXECUTABLE=$python"
    "-DPython_EXECUTABLE=$python"
    '-DQt6_DIR=C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6'
    '-DBUILD_TESTING=ON'
    '-DIICAMERA_WITH_FFMPEG=ON'
    '-DIICAMERA_WITH_NATIVE=ON'
    '-DIICAMERA_WITH_IMAGE_PROCESSING=OFF'
    '-DIICAMERA_BUILD_TOOLS=OFF'
    "-DIICAMERA_FFMPEG_EXECUTABLE=$($workspace.Replace('\', '/'))/Tools/FFmpeg/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe"
)
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake 구성 실패' }
$cache = Get-Content -LiteralPath 'build/CMakeCache.txt' -Raw
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\r?$') {
    throw 'Release 구성이 유실되었음. 같은 구성 인수에 --fresh를 추가하여 다시 구성할 것'
}
& $python (Join-Path $workspace 'SDK/Windows/Configure-WindowsNinja.py') (Join-Path $project 'build')
if ($LASTEXITCODE -ne 0) { throw 'Ninja MSVC 의존성 설정 실패' }
cmake --build build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw '빌드 실패' }
```

모든 생성 파일은 이 프로젝트의 `build/` 아래에 둔다. 컴파일러는 위에서 확인한 절대 경로를 사용한다. 기존 캐시가 다른 compiler 또는 generator를 사용한다면 같은 구성 인수에 `--fresh`를 추가하여 다시 구성한다. MSVC 키트와 MinGW 또는 Debug/Release 라이브러리를 혼합하지 않는다.

`WindowsMsvcPrefix.cmake`와 `Configure-WindowsNinja.py`는 MSVC include 출력 언어를 Ninja에 맞추고 UTF-8 실행 manifest를 적용한다. 빌드 병렬도는 4로 제한하며 실행 검사는 직렬로 수행한다.

## 실행과 확인

본체의 기능 JSON과 장치 수명을 검증한다. 미디어 저장 및 CLI는 별도 Media 소비자에서 검증한다.

```powershell
$dllDirectories = @(Get-ChildItem -LiteralPath build -Recurse -Filter '*.dll' -File |
    Select-Object -ExpandProperty DirectoryName | Sort-Object -Unique)
$env:PATH = (@($dllDirectories) + @($env:PATH)) -join ';'
$env:QSG_RENDER_LOOP = 'basic'
ctest --test-dir build -C Release --output-on-failure --parallel 1 --no-tests=error
if ($LASTEXITCODE -ne 0) { throw '전체 실행 검사 실패' }
```

현재 빌드에 실제 등록된 실행 경로의 예는 `iiCamera.features.json`, `iiCamera.features.bulk`, `iiCamera.features.devices`이다. 전체 목록은 `ctest --test-dir build -N`으로 확인한다.

추가 수동 실행은 다음과 같다. 앞의 DLL 검색 환경을 유지한다.

```powershell
cmake --install build --prefix $prefix
cmake -S example/Media -B build/media-consumer -G Ninja `
    "-DiiCameraIntegration_DIR=$prefix/lib/cmake/iiCameraIntegration" `
    "-DiiFileProviderNative_DIR=$env:USERPROFILE/.local/SDK/iiFileProvider/lib/cmake/iiFileProviderNative"
cmake --build build/media-consumer --config Release
& ./build/media-consumer/iicamera-inspect.exe --help
if ($LASTEXITCODE -ne 0) { throw 'Camera CLI 실행 실패' }
```

## 설치와 배포 범위

전체 테스트가 통과한 구성만 설치한다. skip된 선택 기능은 지원·실행 범위를 별도 기록한다.

```powershell
cmake --install build --config Release --prefix "$prefix"
if ($LASTEXITCODE -ne 0) { throw '설치 실패' }
```

설치 파일은 `build/install_manifest.txt`와 실제 prefix의 파일을 대조한다. 소비자 실행 시 prefix의 `bin`과 필요한 vcpkg DLL을 검색할 수 있어야 한다.

## 검증 근거

- 빌드 정의: [CMakeLists.txt](../CMakeLists.txt)와 프로젝트 테스트 등록이다.
- 이번 실행 결과: [Windows SDK 전체 검증 기록](https://github.com/iisacc-Justmoong/SDK/blob/main/Windows/Verification-20261020.md)이다.
- 프로젝트별 실행 로그와 JUnit은 같은 검증 디렉터리에 보관한다. 로그의 종료 코드, 실패·skip 목록과 실제 실행 범위를 함께 확인한다.

전체 SDK의 실행·설치와 커밋 조건은 [공통 Windows 검증 도구](https://github.com/iisacc-Justmoong/SDK/blob/main/Windows/README.md)에서 재현한다. 원본 JSON·JUnit·상세 로그는 로컬 `Workspace/build/maintenance/20261019-221920-sdk-recovery/final/`에 보관하며 Git에는 포함하지 않는다. 이 프로젝트의 성공은 제품·다른 플랫폼·별도 계정 및 물리 장치의 실연동을 대신하지 않는다.
