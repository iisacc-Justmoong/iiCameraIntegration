# 패키지와 네임스페이스의 표준 이름

모듈 이름은 `iiCameraIntegration`이다. 체크아웃은 `SDK/iiCameraIntegration`,
설치 경로는 `~/.local/SDK/iiCameraIntegration`, 공개 헤더 접두사는
`iiCameraIntegration/`이다. 통합 헤더는
`iiCameraIntegration/iiCameraIntegration.h`이다.

`find_package(iiCameraIntegration 0.4 CONFIG REQUIRED)`와
`iiCameraIntegration::iiCameraIntegration`를 사용한다. 선택적인 사진 처리
컴포넌트는 `iiCameraIntegration::ImageProcessing`이다. C++ 네임스페이스는
`iiCameraIntegration`, 생성된 내보내기 매크로는 `IICAMERAINTEGRATION_EXPORT`,
정적 링크 정의는 `IICAMERAINTEGRATION_STATIC_DEFINE`이다.

네임스페이스도 교정되었으므로 이전 바이너리를 새 라이브러리에 그대로 연결하지
않는다. 소비자의 헤더·네임스페이스·타깃 참조를 수정하고 다시 빌드한다.
체크아웃을 옮긴 후에는 이전 절대 경로가 저장된 CMake 캐시를 새로 구성한다.
카메라 공급자, 장치 수명, 메타데이터 계약은 유지한다. 녹화 호출은 파일 포트를
명시적으로 전달하는 새 서명으로 변경되었으므로 소비자를 수정하고 다시 빌드한다.

`iiCamera.installed_package`는 설치 디렉터리를 이동한 뒤 새 패키지 이름만으로
별도의 C++23 소비자를 빌드·실행한다. Qt 탐색을 비활성화하며 Core와 선택적
ImageProcessing의 설치 계약을 검증한다. 장치 탐색은 권한을 요청하거나 캡처를
시작하지 않는다. 공급자 수명과 미디어 검사는 기존 테스트가 담당한다.
검사 도구는 이제 별도 `example/Media` 소비자에서 구성한다.

사진 처리 패키지 검사는 자체 소비자 캐시를 새로 구성한다. 체크아웃 이동이나
패키지명 교정 후에도 이전 소스의 절대 경로를 재사용하지 않으며, 필수·선택
컴포넌트의 성공·실패 조건과 기존 제한 시간은 유지한다.

`install.sh`는 카메라 SDK만 구성하며 다른 SDK의 설치 순서를 요구하지 않는다.
파일 제공자와의 조합은 별도 `example/Media` 애플리케이션을 구성할 때 명시한다.
macOS 검사 도구는 실제 링크된 SDK의 런타임 경로를 설치 RPATH에 포함한다.

워크스페이스의 `SDK/tests/test_module_names.py`는 디렉터리, 공개 헤더,
Windows 빌드 목록, 동기화 URL 및 관련 제품의 이름 일치를 검사한다.
