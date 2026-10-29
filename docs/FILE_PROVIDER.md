# File creation ownership in iiCameraIntegration

카메라 공통 API는 `src/`, 기기·기능·미디어 구현은 각각 `src/Devices/`,
`src/Features/`, `src/Media/`, 네이티브 어댑터는 `src/Platform/`에 둔다.
헤더와 구현은 같은 책임 디렉터리에 배치하며 기존 공개 include 경로를 유지한다.
설치 소비자에 `IICAMERA_CONSUMER_SKIP_NATIVE_DISCOVERY=ON`을 지정하면 실제 기기를
조회하지 않고 합성 공급자로 패키지의 공개 API를 검증할 수 있다.

카메라 SDK는 파일을 생성하지 않는다. `recordMedia`는 호출자가 제공하는
`RecordingFilePort`로 스트림 생성, 덮어쓰기 없는 게시와 실패 시 정리를 요청한다.
`example/Media`는 이 포트를 iiFileProvider Native에 연결하고 검사 도구의 프레임
출력도 파일 제공자로 생성한다. 캡처·제어·메타데이터·카메라 권한은 SDK가 담당한다.
성공적으로 게시된 녹화와 프레임은 파일 제공자에 프리뷰 처리를 요청한다. 요청은
프리뷰 생성이 완료되었다는 증거가 아니다.

The common [ownership contract](../../iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `src/Media/Media.h`와 `Media.cpp`: 모듈 독립 녹화 계약
- `example/Media/App/FileProviderPort.h`: 파일 제공자 조합
- `example/Media/App/inspect.cpp`와 `example/ImageProcessing/App/process.cpp`: 별도 도구
