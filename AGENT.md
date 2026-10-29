# iiCameraIntegration 작업 지침

C++23과 Qt 없는 공개 API를 유지한다. 소스와 동명 헤더는 src/와 책임별
하위 디렉터리에 함께 둔다. 자동 생성 헤더와 모든 빌드·설치 검증 산출물은 build/에 둔다.

모듈·네임스페이스·헤더·CMake 패키지의 이름은 iiCameraIntegration이다.
소스 변경에는 tests/와 한국어 docs/ 갱신을 포함하며 README.md는 영어로 작성한다.
실제 카메라, 권한 요청과 캡처를 인공 공급자 및 합성 미디어 검증과 구분한다.
기존 사용자 파일과 다른 작업의 변경을 보존한다.
이미지 처리 조합은 `example/ImageProcessing`에서 별도로 구성한다. 카메라 SDK
본체에 이미지 SDK 검색·링크를 다시 넣지 않는다. 녹화 파일 생성과 미완성 파일
정리는 `RecordingFilePort`로 주입하고 `example/Media`에서 파일 제공자와 조합한다.

<!-- workspace-docs:agent:start -->
## 문서 구조와 에이전트 탐색

- 프로젝트 루트의 `README.md`는 사람용 영문 진입 문서이며 `AGENT.md`는 에이전트 진입점이다.
- 하위 인간용 Markdown과 구조화된 agent JSON을 함께 유지한다. Markdown이 원본이며 JSON은 원문 해시와 줄 범위를 가진 파생 문서이다.
- 먼저 [docs/agent-index.json](docs/agent-index.json)에서 관련 문서와 JSON 투영을 찾는다. 원문 SHA-256이 다르면 현재 Markdown을 읽고 투영을 갱신한다.
- JSON의 규칙 후보는 검색용 어휘 일치 결과이다. 과거 기록·예제·실패 상태를 현재 지시나 검증 완료로 자동 해석하지 않는다.
- 기존 프로젝트별 규칙, 동결된 증거, 라이선스, 외부 원본, 사용자 데이터와 다른 작업의 변경을 보존한다.
- 이 문서 재편 지침은 과거 작업의 기능 구현·배포를 자동 재개하라는 요청이 아니다. 현재 사용자의 작업 범위를 우선한다.
- 문서를 수정하면 JSON 쌍·색인을 함께 갱신하고 검증한다. 기존 소스 변경에 대한 빌드·테스트 규칙은 계속 따른다.

Workspace에서 이 프로젝트의 문서를 갱신하는 명령은 다음과 같다.

```sh
python3 ../../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace ../.. sync --project SDK/iiCameraIntegration
```
<!-- workspace-docs:agent:end -->
