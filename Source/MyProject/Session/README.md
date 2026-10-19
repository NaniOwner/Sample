# 멀티플레이 로비

실행 시작 맵은 `/Game/Variant_Shooter/SessionLevel`입니다.
전투 맵의 실제 에셋 이름은 `Lvl_Shooter`입니다 (`Lv1_Shooter`가 아님).

1. 호스트: 방 이름 입력 → **방 생성**. 같은 로비를 `?listen`으로 열어 리슨 서버를 만듭니다.
2. 클라이언트: **방 검색 / 새로고침** → 목록에서 방 선택 → **선택한 방 참가**.
3. 방장: 참가자 목록 확인 → **게임 시작**. `StartSession` 완료 후 서버의 `ServerTravel`로 접속한 플레이어들을 함께 전투 맵으로 이동합니다.
4. 로비의 **방 나가기**는 세션을 정리하고 방 검색 화면으로 돌아갑니다. 호스트 연결이 끊긴 클라이언트도 로비로 돌아갑니다.

현재 검색은 `OnlineSubsystemNull`의 LAN 방식이며 기본 정원은 4명입니다.
Steam/EOS 인터넷 방 검색이나 NAT 중계는 설정하지 않았습니다.
PIE의 Play As Listen Server에서도 방 생성이 가능합니다. 이미 실행 중인 리슨 서버를 재사용하며, 자동 연결된 클라이언트를 유지합니다.
게임 시작 시 Seamless Travel을 사용합니다. PIE에서도 이를 활성화하여 현재 접속과 리슨 서버 포트를 유지한 채 전투 맵으로 이동합니다.
자동 연결된 클라이언트는 이미 로비에 있으므로 방 생성/검색/참가가 비활성화되고 방 나가기를 사용할 수 있습니다.
검색부터 참가까지 직접 시험하려면 각각 독립된 Standalone Game 또는 패키징한 게임으로 실행합니다.

- `SessionLobbySubsystem`: 방 생성/검색/참가/시작/종료, 비동기 결과와 오류, 맵 전환 후에도 유지되는 세션 상태.
- `SessionLobbyGameMode`: 로비 전용 컨트롤러, 정원 검사, 시작 후 참가 제한, seamless travel.
- `SessionLobbyPlayerController`, `SessionLobbyWidget`: 로비 UI, 방장 전용 시작 버튼, 참가자 표시.
- `DefaultEngine.ini`: 시작 맵과 로비 GameMode 매핑, Null 세션 서비스.
- `DefaultGame.ini`: 로비 및 전투 맵을 패키징 대상으로 포함.

개발 빌드에서는 두 프로세스를 `-game -ExecCmds="Lobby.Smoke Host"`와
`-game -ExecCmds="Lobby.Smoke Client"`로 실행해 실제 세션 흐름을 검증할 수 있습니다.
검증은 방 검색, 참가, 클라이언트의 시작 거부, 호스트 시작, 양쪽 전투 맵과 플레이어 Pawn 생성까지 확인하며,
성공 시 각 프로세스 로그에 `LOBBY_SMOKE PASS`를 남깁니다.
