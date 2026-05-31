# webserv

설명: HTTP 서버 구축
상태: 완료

**HTTP 서버를 C++98 버전으로 작성해야 한다.**

# 과제 소개

1. **요구사항**
- 프로그램은 설정 파일을 인수로 받을 수 있어야 한다. 인수가 없다면
기본 경로로 설정 파일을 찾는다.
- `execve()` 로 또 다른 웹서버 프로세스를 띄울 수 없다.
- 서버는 블로킹 되지 않아야 한다.
- 클라이언트와의 연결은 필요시 적절히 종료(거부)되어야 한다.
- 서버는 논블로킹이어야 하며 단 하나의 비동기IO처리 함수(`kqueue`)를 사용하여
모든 처리를 수행해야 한다(`listen` 포함)
- `kqueue`는 반드시 읽기와 쓰기 이벤트를 동시에 감시해야 한다.
- `read`와 `write` 는 반드시 `kqueue` 함수를 통해서만 수행해야 한다.
- `read` , `write`작업 후에 `errno`를 체크하는 행동은 금지된다.
- 설정 파일을 읽기 전에 `kqueue` 를 사용할 필요는 없다.
- `FD_SET, FD_CLR, FD_ISSET, FD_ZERO` 같은 매크로들을 사용할 수 있다.
다만 `select` 같은 초기 버전 IO처리 함수에 필요한 매크로들이라
`kqueue`를 쓰면 사용할 일이 거의 없다.
- 서버는 클라이언트 요청을 무한정 기다리거나 받지 않아야 한다.(timeout 설정)
- 서버는 웹 브라우저와 호환 되어야 한다.
- 서버는 HTTP 1.1 버전을 사용하는 NGINX를 기준으로 만들어야 한다.
- HTTP 응답 코드는 정확해야 한다.
- 서버는 디폴트 에러 페이지를 제공할 수 있어야 한다.
- 서버는 정적 웹사이트를 제공할 수 있어야 한다.
- `fork` 함수로 자식 프로세스를 만드는건 CGI 처리에 한해서만 허용된다.
- 클라이언트는 파일을 서버로 업로드 할 수 있어야 한다.
- `GET` , `POST` , `DELETE`  메서드를 사용할 수 있어야 한다.
- 서버에 스트레스 테스트를 하고, 서버는 살아있어야 한다.
- 서버는 다중 포트를 `listen` 할 수 있어야 한다.

1. **설정 파일**
- 서버마다 사용할 호스트(IP)와 포트를 설정해야 한다.
- `server_names` 는 설정 될 수도 있고 안 될 수도 있다.
- 특정`host:port` 를 가진 서버들 중 첫 번째 서버가 디폴트로 동작한다.
이는 `server_name` 으로 구분 되지 않은 요청이 해당 `host:port`로 들어오면 디폴트 서버인
첫 번째 서버가 이 요청을 처리한다는 뜻.
- 디폴트 에러 페이지가 설정 되어야 한다.
- 클라이언트의 요청 바디 사이즈에 제한이 있어야 한다.
- 클라이언트의 요청 중 URL 처리는 다음을 따른다.
* URL이 허용할 HTTP 메서드를 정의해야 한다.
* 다른 URL로 리디렉션 할 수 있어야 한다.
* URL에 대응하는 파일 시스템 경로를 설정 해야 한다.
* 디렉토리 목록 표시 기능을 활성화하거나 비활성화 해야 한다.
* 요청된 URL이 디렉토리인 경우, 기본으로 제공할 파일을 지정해야 한다.
* `.php` ,`.py` 등의 확장자를 요청하면 CGI를 실행해야 한다.
* `POST` ,`GET` 메서드는 사용 가능해야 한다.
* 클라이언트가 URL로 업로드 요청을 보내면 서버는 특정 디렉토리에
   해당 URL로 업로드한 데이터를 저장하도록 설정해야 한다.
* 클라이언트가 URL로 CGI 파일을 요청하면 `PATH_INFO` 환경 변수에
   파일 경로를 설정하고 CGI 프로그램의 첫 번째 인수로 요청한 CGI 파일을
   주고 실행해야 한다.
* chunked 된 요청이 오면 서버는 해당 요청을 unchunk 시킬 수 있어야 한다.
   CGI는 EOF를 기준으로 바디의 끝을 판단한다.
* 이는 CGI의 출력에도 적용되는 기준이다. `content_length` 가 CGI로부터 반환되지
   않는다면, `EOF`가 반환된 데이터의 끝을 나타낸다.
* 서버 프로그램이 CGI 프로그램을 호출할 때 요청된 파일 경로(URL)를 첫 번째 인자로 줘야 한다.
* CGI 프로그램은 상대 경로를 처리할 수 있어야 한다.
* 서버는 최소한 하나의 CGI 프로그램(ex) php-CGI, Python 등)과 동작해야 한다.

# 개념 정리

HTTP에 관한 거의 모든 정보가 담겨있는 글이다.

https://developer.mozilla.org/ko/docs/Web/HTTP

시스템 호출 함수에 관한 글이다.

https://ko.wikipedia.org/wiki/%EC%8B%9C%EC%8A%A4%ED%85%9C_%ED%98%B8%EC%B6%9C

소켓에 관한 글이다.

https://luckyyowu.tistory.com/71

멀티플렉싱 I/O 함수에 관한 글이다.

https://jacking75.github.io/choiheungbae/%EB%AC%B8%EC%84%9C/epoll%EC%9D%84%20%EC%82%AC%EC%9A%A9%ED%95%9C%20%EB%B9%84%EB%8F%99%EA%B8%B0%20%ED%94%84%EB%A1%9C%EA%B7%B8%EB%9E%98%EB%B0%8D.pdf

https://comfun.tistory.com/entry/12%EC%9E%A5-IO%EB%A9%80%ED%8B%B0%ED%94%8C%EB%A0%89%EC%8B%B1?category=534707#google_vignette

NGINX 설정 파일에 관한 글이다.

https://minimilab.tistory.com/66

# 초안 작성

먼저 웹 서버 프로그램 흐름을 글로 작성해보았다.

초안이기 때문에 최종 결과물과는 다른 부분이 꽤 있다.

프로그램은 설정 파일을 경로 형태로 인수로 받고, 인수가 없다면 기본 경로로 설정 파일을 찾는다.

설정 파일 안에 있는 정보를 담을 여러 클래스에 설정 파일에 있는 데이터를 담아 초기화 시킨다.

*미정) config 관련 클래스를 싱글톤 클래스로 생성하여 편의성을 높일까 고민중.*

→ 일단 협의 후 싱글톤 클래스 말고 매개변수에 참조형으로 넘겨주기로 했다. 생각보다 함수에 매개변수로 넘겨줄 클래스가 많지 않았기 때문.

해당 데이터(IP, port)를 기반으로 서버 소켓을 열고, kqueue 인스턴스를 생성한다.

kqueue 인스턴스에 서버 소켓을 등록하고, kqueue 반복문을 시작한다.

등록된 서버 소켓에서 연결 요청이 들어올때마다 반복문에서 새로운 클라이언트 소켓을 생성한다.

해당 클라이언트 소켓을 kqueue에 등록하고, 클라이언트로부터 들어온 요청 데이터를 HTTP 파서에 보낸다.

HTTP 파서는 해당 요청을 파싱하고 request 객체에 파싱한 정보들을 초기화하고 객체를 반환한다.

반환된 request 객체를 응답을 생성하는 response 로 보내 어떻게 처리할지 결정한다.

예를 들면 정적 파일을 서버 프로그램에서 바로 제공할지, 동적 파일을 제공하기 위해 CGI 프로그램을

자식 프로세스로 호출할지, 아님 에러를 뱉을지..

응답 보낼 데이터가 준비되면 HTTP 응답 표준에 따라 데이터를 가공하고

해당 클라이언트 소켓을 통해 전송한다.

이런 방식으로 kqueue 반복문을 돌다가

일정 시간동안 서버 소켓으로 더이상 요청이 들어오지 않으면(timeout),

kqueue에 등록된 클라이언트 소켓들을 전부 삭제해 연결을 끊는다.

기본적으로 계속 프로그램은 살아있어야 하기 때문에, kqueue 반복문을 무한으로 돌린다.

*미정) 아직 종료 조건은 파악하지 못했다. 시그널이 유력한데 ctrl + c 던 SIGTERM이던*

*시그널이 인터럽트 하면 깨끗한 종료(소켓 닫기 등)을 해야 할것 같다.*

→ 종료하는 방법은 ctrl + c 밖에 없다. 이렇게 종료해도 딱히 leak이 생기진 않아

따로 처리하진 않았다.

# 구현 기록

- **HTTP 요청 파서**
    
    HTTP 요청문의 예시를 가져왔다.
    
    ```cpp
    POST /path HTTP/1.1 // 상태 라인
    Host: www.example.com // 헤더
    Content-Type: application/x-www-form-urlencoded // 헤더
    Content-Length: 27 // 헤더
    
    name=John&age=30&city=Seoul // 바디
    ```
    
    가장 윗줄이 **상태 라인**, `key: value` 값으로 구분된 줄은 **헤더**, 공백 줄 이후의 라인들은 **바디**이다.
    
    상태 라인은 **메서드**(`POST`), **경로**(`/path`), **HTTP 버전**(`HTTP/1.1`) 으로 나뉜다.
    
    메서드는 클라이언트가 어떤 종류의 요청인지 명시한다.
    
    경로는 클라이언트가 서버의 어떤 경로를 통해 요청할지 명시한다.
    
    HTTP 버전은 클라이언트가 사용한 HTTP 버전과 서버측의 버전을 대조하기 위해 명시한다.
    
    각 정보는 **공백 한 칸**으로 구분한다.
    
    헤더는 여러 추가 정보를 `key: value` 값으로 나열한 것이다.
    
    key와 value는 `:` (콜론)으로 구분한다.
    
    바디는 요청이나 응답에 실제로 전달할 데이터이다.
    
    헤더 이후 **공백 줄**로 구분한다. 이외에 공백 줄은 HTTP form에서 존재할 수 없다.
    
    바디 자체는 웹 서버측에서 파싱하지 않는다.
    
    위에 설명한 것들이 HTTP form이며, 이를 어기면 `ERROR`로 처리 해야한다.
    
    이제 코드를 보자.
    
    파싱한 데이터들을 저장할 Request 클래스의 객체를 초기화하는 함수이다.
    
    ```cpp
    void Request::initRequest(ifstream& file)
    {
        string line;
    
        if (!getline(file, line))
            throw runtime_error("Error: Empty status line");
        _parseStatus(line); // 상태 라인 파싱
    
        while (getline(file, line)) // 헤더 파싱 루프
        {
            if (line.empty()) // 헤더와 바디를 구분하는 빈 줄 체크
                break ;
            _parseHeader(line);
        }
    
        while (getline(file, line)) // 바디 초기화
            _body += line + "\n";
    
    }
    ```
    
    먼저 **상태 라인**을 파싱한다.
    
    과제는 `GET`, `POST`, `DELETE` 이외의 메소드는 구현할 필요가 없다고 했기 때문에
    
    이외의 메서드나 문자열은 예외처리한다.
    
    버전도 HTTP 1.1 버전인지 확인한다.
    
    ```cpp
    void Request::_parseStatus(string line)
    {
        if (count(line.begin(), line.end(), ' ') != 2) // 상태 라인에 공백이 2개 이상 있으면 오류
            throw runtime_error("Error: Wrong status line form (space)");
    
        istringstream stream(line);
    
        stream >> _method;
        if (_method != "GET" && _method != "POST" && _method != "DELETE") // 과제에 명시된 메서드인지 체크
            throw runtime_error("Error: Wrong method");
    
        stream >> _url;
        _parseUrl();
    
        stream >> _version;
        if (_version != "HTTP/1.1") // 1.1버전인지 체크
            throw runtime_error("Error: Wrong version");
    }
    ```
    
    Url 파싱은 쿼리문과 경로를 나누는 것이 핵심이다. 구분자는 `?` 이다.
    
    Url은 시작할 때  `/` 로 시작하지 않으면 오류이다.
    
    ```cpp
    void Request::_parseUrl()
    {
        if (!_url.empty() && _url[0] != '/') // Url이 존재하는데 / 로 시작하지 않는다면 오류
            throw runtime_error("Error: Wrong url");
    
        size_t pos = _url.find('?'); // 쿼리 구분자 체크
    
        if (pos == string::npos)
        {
            _path = _url;
        }
        else
        {
            _path = _url.substr(0, pos);
            _query = _url.substr(pos + 1, _url.size() - 1);
        }
    }
    ```
    
    <aside>
    💡
    
    프록시 서버는 `https://` 로 시작하는 원문 Url로 받는다. 그러나 웹 서버는 HTTP 요청문을 받을 때 프록시든, 클라이언트든 간에 scheme과 domain을 제외한 경로부터 Url로 받는다.
    
    </aside>
    
    다음은 **헤더**를 파싱한다.
    
    헤더는 key 와 value 값으로 구분되며, 구분자는 `:` 이다.
    
    `:` 이후에 공백은 자유롭게 사용 가능하기 때문에 공백을 무시하고 value값을 가져온다.
    
    ```cpp
    void Request::_parseHeader(string line)
    {
        size_t delimiter_pos = line.find(':'); // key 와 value의 구분자인 ':'을 체크
    
        if (delimiter_pos == string::npos)
            throw runtime_error("Error: No delimiter found in header");
    
        string key = line.substr(0, delimiter_pos);
    
        size_t value_pos = line.find_first_not_of(" \t", delimiter_pos + 1); // ':' 이후 공백을 무시
    
        if (value_pos == string::npos)
            throw runtime_error("Error: Wrong header value");
    
        string value = line.substr(value_pos, line.size() - 1);
    
        _headers[key] = value; // key, value 쌍 삽입
    }
    ```
    
    추가 팁:
    
    1. unordered_map으로 []로 key 값 기반 접근을 할때
        
        `find()` 나 `at()` 함수를 사용하면 좋을듯. 만약 함수가 실패하면 예외처리.
        
        `at()` → 해당 key 가 있는지 확인하고 있다면 해당 위치의 Value를 반환.
                    없다면 `std::out_of_range` 예외를 반환. 
        
        `find()` → 해당 key가 있는지 확인하고 있다면 해당 위치의 이터레이터를 반환.
                            없다면 `end()`반환.
        
    2. unordered_map의 key 값이 인덱스 숫자가 아니기 때문에 이터레이터로 순회해야 한다.
    
    구현 후 생각난 문제점 :
    
    헤더는 무조건 한 줄인가? 만약 아니면 파싱이 달라진다.
    
    다행이 이젠 한줄이다. GPT의 답변 →
    
    과거 HTTP/1.1 명세에서는 너무 긴 헤더를 나눠 작성할 수 있도록 했습니다. 이 경우 개행 문자(`\r\n`) 뒤에 **공백**(스페이스 또는 탭)을 추가해 이어지는 줄임을 나타냈습니다.
    
    ### 하지만!
    
    **RFC 7230**(2014)에서 이 줄바꿈 방식은 보안 및 구현상의 문제 때문에 **폐기**되었습니다. 이제 대부분의 구현체는 헤더를 한 줄로 작성해야 합니다.
    

- **메인 로직**
    - 메인 함수
        
        설정 파일을 인수로 받고 해당 파일을 파싱 후 데이터를 `ServConf` 클래스 `conf` 객체에 저장.
        
        `conf` 객체에 담긴 정보로 서버 소켓을 열고 `kqueue` 생성.
        
        클라이언트 연결 요청마다 클라이언트 소켓을 생성 후 `kqueue`에 삽입.
        
        이후 서버 소켓, 클라이언트 소켓으로 오는 모든 요청을 `kqueue` 로 반복문에서 처리한다.
        
        ```cpp
        #include "Server.hpp"
        #include "../config/ServConf.hpp"
        
        int main(int argc, char** argv)
        {
            string config;
            if (argc == 1)
                config = "file/nginx.conf";
            else if (argc == 2)
                config = argv[1];
            else
            {
                cerr << "Usage: ./webserv [config file path]" << endl;
                return (1);
            }
        
            try
            {
                ServConf conf(config);
                Server serv(conf);
        
                while (true)
                {
                    cout << "\rWaiting..." << flush;
                    int nev = serv.getKevent(); // 이벤트 리스트에서 이벤트 갯수를 가져옴
        
                    for (int i = 0; i < nev; i++) //  이벤트 갯수만큼 반복
                    {
                        // 리스트에서 이벤트 하나씩 가져오기
                        const struct kevent& event = serv.getEvList(i);
                        // 서버 소켓의 이벤트인지 확인
                        if (serv.getServerIdx(event.ident) != -1)
                            serv.acceptClient(event.ident);
                        // 아니라면 클라이언트 소켓 이벤트
                        else if (serv.getClientIdx(event.ident) != -1)
                        {
                            if (event.flags & EV_EOF) // 클라이언트 쪽에서 연결을 닫았을때
                                serv.closeClient(event.ident);
                            else if (event.filter == EVFILT_READ) // 읽기
                                serv.readClient(event.ident, conf);
                            else if (event.filter == EVFILT_WRITE) // 쓰기
                                serv.sendClient(event.ident, conf);
                        }
                        else
                            throw runtime_error("Error: Failure to kqueue event handler");
                    }
                    serv.checkTimeout(conf.getAliveTime()); // timeout 체크
                }
            }
            catch(const exception& e)
            {
                cerr << e.what() << '\n';
            }
        
            return (0);
        
        ```
        
    - 설정 관련
        
        설정 파일의 예시를 가져왔다.
        
        ```bash
        http {
            include             /Users/seunghan/42_projects/webserv/master/file/mime.types;
            default_type        application/octect-stream; # 디폴트 타입은 바이너리
        
            keepalive_timeout   60;
        
            # CGI 제공 서버
            server {
                listen      4242; # 포트 번호
                server_name localhost; # Host 헤더와 비교하여 서버 블록 선택
                root        /Users/seunghan/42_projects/webserv/master/html;  # url 맨 앞인 '/' 문자가 이 경로로 대체됨. 루트 경로.
        
                client_max_body_size    2024; # 바이트 단위
        
                error_page  400 401 403 404     /error_page/40x.html;
                error_page  500 501 502 503 504 /error_page/50x.html;
        
                # location 블록은 특정 요청 url 을 처리할 작업을 지정할 수 있다.
                # location 오른쪽에 url 패턴 지정 가능
                location / { # 루트 경로
                    index   index.html index2.html index3.html; # 요청 url 끝이 디렉토리일때 기본적으로 제공할 파일
                    autoindex   on; # 만약 원하는 index 파일이 없다면 (ex) index4.html) 디렉토리 내부 파일 목록 제공
                }
                location /favicon.ico { # 아이콘
                    root    /Users/seunghan/42_projects/webserv/master/html/assets; # 루트 경로를 바꿔서
                    index   favicon.ico; # 원하는 파일 제공
                }
                location .py$ { # CGI
                    cgi_pass    /usr/bin/python3; # cgi 프로그램 경로
                }
            }
        
            # CGI 미제공 서버
            server {
                listen      8080;
                server_name localhost;
                root        /Users/seunghan/42_projects/webserv/master/html;
        
                client_max_body_size    1024;
        
                location / {
                    index   index.html;
                    autoindex   on;
                }
                location /return {
                    index   index.html index2.html index3.html; # 무시
                    # 리디렉션 정의, 즉시 autoindex.html로 가라는 리디렉션 요청을 보냄.
                    return  301 /autoindex.html;
                }
                location /post {
                    autoindex on;
                    return  301 /feature/post.html;
                }
                location /delete {
                    return  301 /feature/delete.html;
                }
                location /text {
                    return  200 "Hello, \"World\"";
                }
            }
        }
        ```
        
        각 옵션 중 설명이 필요한 것들은 주석으로 달아놓았다.
        
        블록이 많은데, 각 블록의 스코프는 중괄호로 구분이 가능하다.
        
        따라서 블록의 범위 순서는 :
        
        HTTP 블록(`ServConf`) > Server 블록(`ServBlock`) > location 블록(`LocBlock`)
        
        블록의 종류에 따라 담을 정보가 달라 각각의 클래스를 선언했다.
        
        - `ServConf` 클래스
            
            ```cpp
            #ifndef SERVCONF_HPP
            # define SERVCONF_HPP
            
            #include <vector>
            #include <map>
            #include <fstream>
            #include <iostream>
            #include <exception>
            #include "ServBlock.hpp"
            
            using namespace std;
            
            // 전체적인 내용 및 http 블록 관련 클래스
            // ServBlock나 default_type이 정의 안 되어 있는 경우 에러 처리
            class ServConf
            {
            private:
                long _aliveTime; // keepalive_timeout, 디폴트는 75초
                // 서버 블록을 conf 파일 위에서 아래 순서로 벡터에 담음
                vector<ServBlock> _serv;
                map<string, string> _mime; // MIME type -> key:value
            
                void _includeFile(const string& fileName);
                void _parseMime(ifstream& file);
                void _parseHTTP(ifstream& file, bool inc); // include를 통해서 들어간 경우 true, 그 외에 false
                void _parse(ifstream& file);
            public:
                ServConf(const string& fileName);
                ~ServConf();
            
                const long& getAliveTime() const;
                const ServBlock& getServBlock(size_t idx) const;
                const vector<ServBlock>& getServ() const;
                const string& getMime(const string& key) const;
            };
            
            #endif
            ```
            
        - `ServBlock` 클래스
            
            ```cpp
            #ifndef SERVBLOCK_HPP
            # define SERVBLOCK_HPP
            
            #include <vector>
            #include <map>
            #include <fstream>
            #include <iostream>
            #include <exception>
            #include "LocBlock.hpp"
            
            using namespace std;
            
            // Server 블록 클래스
            // 서버 포트, 클라이언트 바디 사이즈, location block 정의 안 되어 있는 경우 에러 처리
            class ServBlock
            {
            private:
                int _maxSize;                           // 클라이언트 최대 바디 사이즈
                string _port;                           // 서버 포트 넘버
                string _root;                           // root 위치 정보
                vector<string> _name;                   // 서버 이름 정보
                pair<string, string> _return;           // 리다이렉션 정보
                map<long, string> _error;       // key: status code, value: page
                map<string, LocBlock>_path; // path에 따른 location 정보
            
                void _parseLine(vector<string>& tokens);
                void _parseBlock(vector<string>& tokens, ifstream& file);
            public:
                ServBlock();
                ~ServBlock();
            
                void parseServBlock(ifstream& file);
            
                const int& getMaxSize() const;
                const string& getPort() const;
                const string& getRoot() const;
                const vector<string>& getName() const;
                const pair<string, string>& getReturn() const;
            
                const map<long, string>& getErrorPage() const;
                const map<string, LocBlock>& getPath() const;
                const map<string, LocBlock>::const_iterator getPathIter(const string& path) const;
            };
            
            #endif
            ```
            
        - `LocBlock` 클래스
            
            ```cpp
            #ifndef LOCBLOCK_HPP
            # define LOCBLOCK_HPP
            
            #include <vector>
            #include <fstream>
            #include <iostream>
            #include <exception>
            
            using namespace std;
            
            #define GET 0
            #define POST 1
            #define DELETE 2
            
            // location 블록 클래스
            class LocBlock
            {
            private:
                bool _autoindex;                // autoindex on/off
                bool _method[3];                // 허용 메서드 (GET, POST, DELETE)
                string _root;                   // root 정보
                string _cgiPass;                // cgi_pass 정보
                vector<string> _index;          // index 정보
                pair<string, string> _return;   // 리다이렉션 정보 <status code, url>
            
                void _parseLine(vector<string>& tokens);
            public:
                LocBlock();
                ~LocBlock();
            
                void parseLocBlock(ifstream& file, const string& path);
            
                const bool& getAutoindex() const;
                const bool& getMethod(int method) const;
                const string& getRoot() const;
                const string& getCgipass() const;
                const vector<string>& getIndex() const;
                const pair<string, string>& getReturn() const;
            };
            
            #endif
            ```
            
    - 서버 관련
        
        소켓과 `kqueue`를 여기서 다루기 때문에, 사실상 메인 로직의 핵심이다.
        
        따라서 메서드 정의 분량이 길다.
        
        - 클래스 정의
            
            ```cpp
            #define MAX_EVENTS 128
            
            class Server
            {
            private:
                int _kq; // kqueue fd
                struct timespec _timeout;
                vector<struct kevent> _evList; // 이벤트 모음 벡터
            
                struct addrinfo _hints; // hint 구조체
                map<int, int> _server; // <서버 소켓 fd, 서버 블록 인덱스>
                map<int, Client> _client; // <클라이언트 소켓 fd, 클라이언트 클래스>
            
                void _setAddrInfo();
                int _initSocket(const char* domain, const char* port);
                void _setSocket(const vector<ServBlock>& serv);
                void _setKqueue();
                void _setEvent(int fd, int filter, int flags);
                void _sendError(int fd, const string& status, const string& phrase, const ServConf& conf);
                void _printLog(int fd, const string& one, const string& two, const string& color);
            public:
                Server(ServConf& servConf);
                ~Server();
            
                void acceptClient(int fd);
                void readClient(int fd, const ServConf& conf);
                void sendClient(int fd, const ServConf& conf);
                void closeClient(int fd);
            
                void checkTimeout(long timeout);
            
                int getServerIdx(int fd) const;
                int getClientIdx(int fd) const;
            
                int getKq() const;
                int getKevent();
                const struct kevent& getEvList(int idx) const;
            };
            
            #endif
            ```
            
        - 메서드 정의
            
            코드를 `Server.cpp` 파일 위에서 아래 순서로 볼건데,
            
            간단한 설명은 주석을 달고 긴 설명은 따로 글로 작성했다.
            
            ```cpp
            #include "Server.hpp"
            
            Server::Server(ServConf& sc)
            {
                const vector<ServBlock>& serv = sc.getServ(); // 서버 블록 가져옴
            
                _setAddrInfo(); // hint 구조체 초기화
                _setSocket(serv); // 소켓 설정
                _setKqueue(); // kqueue 설정 및 실행
            
                _timeout.tv_sec = 1;
                _timeout.tv_nsec = 0;
            }
            
            Server::~Server()
            {
            		// 소켓 전부 닫기
                for (map<int, int>::iterator it = _server.begin(); it != _server.end(); it++)
                    close(it->first);
            }
            ```
            
            생성자와 소멸자.
            
            ```cpp
            void Server::_setAddrInfo()
            {
                memset(&_hints, 0, sizeof(_hints));
                _hints.ai_family = AF_INET; // IPv4
                _hints.ai_socktype = SOCK_STREAM; // TCP
                _hints.ai_flags = AI_PASSIVE; // 서버 소켓(수신용), 설정시 들어오는 모든 연결 수신 가능
                                              // 미설정시 클라이언트(액티브) 소켓
            }
            ```
            
            `hint` 구조체를 초기화하는 함수이다.
            
            `hint` 구조체는 `addrinfo` 구조체의 인스턴스로, 소켓을 열기 위해
            
            `bind()` 가 가능한 IP 주소들을 검색하는`getaddrinfo()` 함수에서
            
            검색 조건을 설정하는 구조체이다.
            
            쉽게 말해 특정 조건에 맞는 주소만 필터링 해주는 구조체.
            
            ```cpp
            int Server::_initSocket(const char* domain, const char* port)
            {
                struct addrinfo *res, *p;
            
                int status = getaddrinfo(domain, port, &_hints, &res);
                if (status != 0)
                    throw runtime_error("\rError: getaddrinfo: " + string(gai_strerror(status)));
                    
            	  int tmpfd;
                // res 포인터에 있는 리스트를 순회하며 각 노드마다 서버 소켓 생성이 가능한지 체크
                for (p = res; p != NULL; p = p->ai_next)
                {
                    tmpfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                    if (tmpfd == -1)
                        throw runtime_error("\rError: socket: " + string(strerror(errno)));       
            ```
            
            소켓을 설정하고 `bind()` 하는 함수이다.
            
            `getaddrinfo()` 함수는 `res` 포인터에
            
            소켓 설정을 준비시킨 `addrinfo` 구조체 리스트 시작 주소를 저장한다.
            
            이때 매개 변수인 도메인 이름(`domain` ), 포트(`port` ), `hint` 구조체(`_hints` ) 정보를
            
            기반으로 `addrinfo` 구조체를 초기화 시킨다.
            
            쉽게 말해 소켓에 바인딩 시킬 IP 주소와 포트의 정확한 값을 내부적으로 지정해 주는 함수이다.
            
            한 도메인이 여러 IP 주소를 가질 수 있기 때문에 `addrinfo` 는 여러개 일 수 있다.
            
            따라서`res` 에는`addrinfo`리스트의 주소가 저장된다.
            
            ```cpp
                    int opt = 1;
                    if (setsockopt(tmpfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) == -1)
                        throw runtime_error("\rError: setsockopt: " + string(strerror(errno)));
            
                    if (::bind(tmpfd, p->ai_addr, p->ai_addrlen) == 0) // 소켓 생성(bind)이 한번이라도 성공시 break
                        break ;
            
                    close(tmpfd);
                }
                freeaddrinfo(res); // 리스트 동적할당 해제
                if (p == NULL)
                    throw runtime_error("\rError: Failed to bind with following domain: " + string(domain));
            
                if (listen(tmpfd, MAX_EVENTS) == -1) // 생성한 서버 소켓을 수신 대기 상태로
                    throw runtime_error("\rError: listen: " + string(strerror(errno)));
            
                return (tmpfd);
            }
            ```
            
            `initSocket()` 의 나머지 부분이다.
            
            `setsockopt()` 는 소켓 옵션을 활성화/비활성화 하기 위한 함수이다.
            
            `opt = 1` 이면 활성화이고, `SO_REUSEADDR` 옵션을 활성화 시켰다.
            
            이 옵션은 소켓이 닫혀 `TIME_WAIT` 상태에 있어도 대기하지 않고
            
            같은 포트로 `bind()` 함수를 바로 사용 가능하게 하는 옵션이다.
            
            이게 가능한 이유는 TCP 통신의 **시퀀스 넘버** 덕분이다.
            
            - **`TIME_WAIT`과 시퀀스 넘버(Sequence Number)**
                
                소켓을 닫아 연결을 끊으면 `ESTABLISHED` → `TIME_WAIT` 으로 상태로 바뀌는데
                
                `TIME_WAIT` 상태란, **TCP 연결을 종료한 후 일정 시간 동안 지연된 패킷이나 잃어버린 패킷을 처리할 시간을 확보하고, 새로운 연결이 이전 연결과 충돌하지 않도록 보호하기 위한 상태이다.**
                
                만약 이 상태에 들어간 클라이언트 소켓의 포트 번호에 서버측에서 새로운 소켓을 생성해
                
                해당 포트 번호에 `bind()` 를 시도한다면 함수가 실패한다.
                
                이걸 해결하기 위한 방법이 있는데,
                
                `SO_REUSEADDR` 옵션을 사용하면 닫힌 소켓의 `TIME_WAIT`을 무시하고 해당 소켓이 사용하던
                
                포트를 즉시 재사용하게 만들어 `bind()` 함수가 성공하게 만들 수 있다.
                
                그럼 `TIME_WAIT`을 무시하고 같은 포트로 새로운 소켓을 열어 연결해버리면
                
                잔류 데이터가 새로운 연결로 가버리지 않을까? 의문이 들었다.
                
                그러나 TCP 연결에선 **시퀀스 넘버**라는 헤더를 사용하여
                
                각 연결에 랜덤한 고유 번호를 부여한다.
                
                따라서 새로운 연결로 이전 연결의 잔류 데이터가 와도 해당 패킷의 시퀀스 넘버를 따져
                
                다른 시퀀스 넘버를 가진 패킷이면 무시한다.
                
                <aside>
                💡
                
                시퀀스 넘버는 연결을 구분 지음과 동시에 바이트가 얼마나 전송되었는지
                
                바이트 크기만큼 시퀀스 넘버에 더하면서 확인하는 역할도 한다.
                
                </aside>
                
            
            ```cpp
            void Server::_setSocket(const vector<ServBlock>& serv)
            {
                int idx = 0;
                // 가져온 서버 블록들 순회
                for (vector<ServBlock>::const_iterator it = serv.begin(); it != serv.end(); it++)
                {
                    const char* port = it->getPort().c_str(); // 포트 번호 가져옴
            
                    if (it->getName().size() != 0) // server_name 이 있다면
                    {
                        // server_name 갯수 만큼 소켓 생성
                         for (vector<string>::const_iterator nit = it->getName().begin(); nit != it->getName().end(); nit++)
                            _server[_initSocket((*nit).c_str(), port)] = idx; // domain(server_name), port 넘김
                    }
                    else // server_name 이 없다면
                        _server[_initSocket(NULL, port)] = idx; // domain 없이 포트만 넘김
                    idx++;
                }
            }
            ```
            
            각 서버 블록에 있는 `server_name` 상태에 따라 `initSocket()` 을 호출하는 함수이다.
            
            즉 도메인이 있으면 매개변수로 넘기고, 없으면 `NULL` 을 넘긴다.
            
            `getaddrinfo()` 함수는 `domain` 매개변수가 `NULL` 이고 `AI_PASSIVE` 플래그가 있으면
            
            IP를 `INADDR_ANY(0.0.0.0)` 로 설정한다고 한다. 이는 모든 연결을 받는다는 의미로,
            
            서버 소켓이 주로 사용하는 설정이다.
            
            `server_name` 갯수만큼 소켓을 생성하는 부분은 잘 못 된거 같다.
            
            소켓 생성 기준은 `server_name` 이 아니라 `listen` 이 기준이라고 한다.
            
            ```cpp
            void Server::_setKqueue()
            {
                _kq = kqueue(); // kqueue 생성
                if (_kq == -1)
                    throw runtime_error("\rError: kqueue: " + string(strerror(errno)));
                // kqueue 는 이벤트 리스트로 정적 배열을 사용하기 때문에
                // 벡터에 미리 사이즈를 할당해주고 정적 배열처럼 사용
                _evList.resize(MAX_EVENTS);
            
                for (map<int, int>::iterator it = _server.begin(); it != _server.end(); it++)
                {
                    struct kevent evSet; // 이벤트 구조체
                    // 클라이언트로부터 온 연결 요청을 read 하는 이벤트를 세팅
                    // 서버 소켓 fd 를 제공함으로써 해당 fd 에 대한 이벤트임을 명시
                    // EV_ADD = 이벤트 추가, EV_ENABLE = 이벤트 활성화 -> 두개를 동시에 설정
                    EV_SET(&evSet, it->first, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
                    // 이벤트를 kqueue 에 등록
                    // kevent 두번째 매개 변수에 이벤트 객체 주소를 주면 이벤트를 등록함
                    if (kevent(_kq, &evSet, 1, NULL, 0, NULL) == -1)
                        throw runtime_error("\rError: kevent: " + string(strerror(errno)));
                }
            }
            ```
            
            `kqueue` 를 생성하고 초기 이벤트 세팅을 하는 함수이다.
            
            서버 소켓 fd에 오는 클라리언트의 연결 요청을 READ할 이벤트를 만들고`kqueue`에 등록한다.
            
            `EV_SET`  매크로 함수 4번째 매개 변수를 보면 `|` 가 있는데,
            
            이는 비트 플래그에서 사용되는 방법이다.
            
            - **비트 플래그란?**
                
                예를 들어 0001, 0010, 0100 의 상태를 가진 비트가 있다고 하면
                
                각 1에 해당하는 숫자를 플래그가 on된 상태로 정의.
                
                각 비트를 매크로로 정의하여 플래그로써 사용이 가능하다.
                
                플래그의 상태는 & 연산자로 확인 가능하고,
                
                플래그를 설정할 때는 | 연산자로 해당 자리를 무조건 1로 만들어주어 on 할 수 있다.
                
            
            ```cpp
            
            void Server::_setEvent(int fd, int filter, int flags)
            {
                struct kevent evSet;
                // fd, filter = 이벤트 종류, flags = 이벤트 플래그(추가, 삭제등)
                EV_SET(&evSet, fd, filter, flags, 0, 0, NULL);
                if (kevent(_kq, &evSet, 1, NULL, 0, NULL) == -1)
                    throw runtime_error("Error: HERE: " + string(strerror(errno)));
            
                _client[fd].updateTime();
            }
            ```
            
            `kqueue` 에 등록할 이벤트를 만들고 등록하는 함수이다.
            
            매개 변수로 fd, 이벤트 필터, 플래그를 받아 해당 정보를 기반으로 이벤트를 생성 및 등록한다.
            
            ```cpp
            int Server::getKevent()
            {
                // kevent 4번째 매개 변수에 이벤트 리스트 시작 주소를 주면 이벤트들을 가져옴
                // 쉽게 말해 커널 kqueue에서 발생한 이벤트들을 MAX_EVENTS 만큼 리스트에 저장시킴.
                int nev = kevent(_kq, NULL, 0, _evList.data(), MAX_EVENTS, &_timeout);
                if (nev == -1)
                    throw runtime_error("\rError: kevent: " + string(strerror(errno)));
                return (nev);
            }
            ```
            
            발생한 이벤트를 가져오는 함수이다.
            
            `_evList` 벡터의 시작 주소를  `kevent` 함수의 매개 변수로 주면 발생한 이벤트를
            
            벡터에 채워넣는다.
            
            `kevent` 가 많이 등장하는데, 복잡한 부분이 꽤 있다. 한번 정리해보았다.
            
            - `kevent` 정리
                
                구조체, 함수 이름이 똑같음. 헷갈림 주의.
                
                `kevent` 구조체 : 1. 감시할 fd 지정
                                         2. fd의 어떤 이벤트를 감시할건지 플래그 지정
                                         3. 감시중에 이벤트가 포착되면 어떤 행동을 수행할건지 지정
                                         4. 지정하는 함수는 `EV_SET`
                
                `kevent` 함수 : 1. `kqueue` 인스턴스 fd
                                      2. `kqueue`에 추가 혹은 수정, 삭제할 이벤트 (`changelist`)
                                      3. 2번의 이벤트 갯수
                                      4. 발생할 이벤트를 저장할 배열 시작주소 포인터 (`eventlist`)
                                      5. 발생한 이벤트 모음 배열의 크기
                                      6. timeout 지정
                                      `return` = 발생한 이벤트 갯수
                
                2번째 매개 변수가 `NULL` 이 아니면 이벤트를 `kqueue`에 등록하는 함수로서 작동하고,
                
                4번째 매개 변수가 `NULL` 이 아니면 포인터로 들어온 배열에
                
                발생한 이벤트들을 채워줌으로써 이때까지 발생한 이벤트들을 가져오는 함수로 작동한다.
                
                이벤트를 가져오는 경우 발생한 이벤트가 없다면 대기(블로킹)되는데,
                
                마지막 매개변수인 timeout을 통해 블로킹 시간을 지정할 수 있다.
                
                `kevent` 함수가 **fd(이벤트)등록과 이벤트 가져오기 혹은 대기 모두 하는 함수**로 설계된 이유는
                
                보통 fd를 등록하면 해당 fd에서 발생할 이벤트를 대기하는 순서로 진행되기 때문이다.
                
                `kevent` 함수는 커널 이벤트를 조작하는 함수로서 시스템 콜이 호출된다.
                
                시스템 콜은 그 자체로 비용이 비교적 높은 작업인데, 이벤트 등록과 대기를 분리하면
                
                시스템 콜이 두 번 호출되는 반면 하나의 함수로 통합하면 한 번의 호출로 두 개의 작업이 가능하다.
                
                ### **매개변수에 따른 동작 요약**
                
                | 매개변수 (`changelist`, `eventlist`) | 동작 |
                | --- | --- |
                | `changelist != NULL` + `eventlist != NULL` | **이벤트 등록/수정 후 발생한 이벤트 가져오기**. 등록한 이벤트와 발생한 이벤트를 동시에 처리. |
                | `changelist != NULL` + `eventlist == NULL` | **이벤트 등록/수정만 수행**. 발생한 이벤트는 가져오지 않음. |
                | `changelist == NULL` + `eventlist != NULL` | **발생한 이벤트 감지만 수행**. 등록된 이벤트 중 발생한 이벤트를 가져옴. |
                | `changelist == NULL` + `eventlist == NULL` | 아무 작업도 수행하지 않음. |
            
            ```cpp
            string getErrorPage(const std::string& status, const std::string& phrase) // 에러 페이지 직접 작성
            {
                string error = "<!DOCTYPE html>\n"
                                "<head>\n"
                                "   <title>Error</title>\n"
                                "</head>\n"
                                "<body>\n"
                                "   <h1>" + status + "</h1>\n"
                                "   <p>" + status + " " + phrase + "</p>\n"
                                "</body>\n"
                                "</html>";
                return (error);
            }
            ```
            
            준비된 에러 페이지가 없다면 직접 작성하는 함수.
            
            상태 코드(`status`)와 이유(`phrase`)를 매개변수로 받는다.
            
            ```cpp
            void Server::_sendError(int fd, const string& status, const string& phrase, const ServConf& conf)
            {
                string error;
                const string& root = conf.getServBlock(_client[fd].getIndex()).getRoot(); // conf 의 root 가져옴
                const map<long, string>& temp = conf.getServBlock(_client[fd].getIndex()).getErrorPage(); // _error<상태 코드(status), 에러 페이지>
                map<long, string>::const_iterator it = temp.find(strtol(status.c_str(), NULL, 10)); // _error 에서 status 를 탐색
            
                if (it != temp.end()) // 찾았다면
                {
                    ifstream ifs(root + it->second); // 에러 페이지 경로 설정 후 열기
                    if (!ifs) // 열기에 실패하면
                        error = getErrorPage(status, phrase); // 에러 페이지 직접 작성
                    else // 성공 시
                    {
                        stringstream buf;
                        buf << ifs.rdbuf(); // 에러 페이지 내용 추출
                        error = buf.str(); // string error 에 복사
                    }
                }
                else // 찾지 못하면
                    error = getErrorPage(status, phrase); // 직접 작성
            
                // 응답 헤더 작성
                ostringstream oss;
                oss << error.size(); // 에러 페이지가 본문이라 사이즈 측정 후
                string response = "HTTP/1.1 " + status + " " + phrase + "\r\n"
                                "Content-Type: text/html\r\n"
                                "Content-Length: " + oss.str() + "\r\n" // Content-Length 에 사이즈 명시
                                "\r\n" +
                                error;
            
                // 응답 write, 에러이기 때문에 실패, 성공 둘다 소켓은 close
                // 이 함수에서 체크하는 에러 코드는 413, 431 -> 이 경우 소켓은 무조건 닫아야함
                // 413 -> body > maxsize, 431 -> header > 8192
                if (write(fd, response.c_str(), response.size()) <= 0)
                    closeClient(fd);
                closeClient(fd);
            }
            ```
            
            에러 상황을 핸들링하는 함수.
            
            에러 파일을 열기위해 `root` 경로를 가져오고,
            
            매개변수로 들어온 `status` 에 맞는 페이지를 검색하기 위해
            
            `_error` 컨테이너를 가져온다.
            
            찾으면 파일을 읽고 아니면 직접 작성.
            
            헤더는 직접 작성한다. 에러 페이지 본문 사이즈 측정 후 `Content-Length` 헤더에 기입.
            
            에러 페이지를 응답하기 위해 `write` 한다.
            
            이 함수는 웹 서버가 응답을 준비하기 전에 미리 에러를 체크하는 구간이므로,
            
            체크하는 에러 코드가 정해져 있다. `413` 과 `431` .
            
            이유는 주석에 있다. 이 에러들은 즉시 소켓을 닫아야 한다.
            
            ```cpp
            void Server::acceptClient(int fd)
            {
                // 클라이언트의 IPv4 주소, 포트 넘버를 저장하는 구조체
                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);
            
                // 클라이언트의 연결 요청을 accept
                // fd = 서버 소켓, &addr = 클라이언트의 정보 저장 구조체 주소
                // 연결 성공시 클라이언트 소켓의 fd 반환
                int client_fd = accept(fd, (struct sockaddr*)&addr, &addr_len);
                if (client_fd == -1)
                    throw runtime_error("\rError: accept: " + string(strerror(errno)));
                // 서버 소켓을 논 블로킹으로 설정
                if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
                    throw runtime_error("\rError: fcntl(F_SETFL, O_NONBLOCK)");
            
                // 클라이언트의 IP 주소와 포트 넘버를 저장
                // IP 는char 배열으로 저장, 사이즈는 INET_ADDRSTRLEN
                char ip[INET_ADDRSTRLEN];
                // IP 를 사람이 읽을 수 있는 문자열로 변환하는 함수
                // addr 에 담긴 IP 주소를 변환 후 char ip[] 에 저장
                inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
                // addr 에 담긴 포트 번호는 빅 엔디언 바이트 순서로 저장됨
                // ntohs = 포트 넘버를 빅 엔디언 -> 로컬 시스템 엔디언으로 변환
                int port = ntohs(addr.sin_port);
            
                // key = 클라이언트 fd, value = Client 클래스
                // 키 값에 대응하는 Client 클래스 생성 후 삽입
                // 클래스엔 포트 넘버, IP,  서버 블록 인덱스 포함
                _client[client_fd] = Client(port, getServerIdx(fd), ip);
            
                // 클라이언트가 보내는 요청을 클라이언트 소켓이 read 하기도 하고
                _setEvent(client_fd, EVFILT_READ, EV_ADD | EV_ENABLE);
                // 읽은 후 생성한 응답을 클라이언트에게 write 해줘야 함
                // 다만 읽은 다음 응답문 생성이 가능하면 생성 후 write 해줘야 하기 때문에 일단 DISABLE
                _setEvent(client_fd, EVFILT_WRITE, EV_ADD | EV_DISABLE);
            
                _printLog(client_fd, "Connected", "", YELLOW);
            }
            ```
            
            서버 소켓이 클라이언트의 연결 요청을 `accept` 하는 함수다.
            
            `accept` 에 성공하면 일단 서버 소켓을 논 블로킹으로 설정한다.
            
            서버 소켓을 논 블로킹으로 설정하는 이유는 클라이언트의 연결 요청을 기다릴 때
            
            프로그램을 블로킹 시키지 않기 위해서인데, 이 시점에 논 블로킹 설정을 한 이유는
            
            첫 연결 전에는 아무 요청이 없어 블로킹으로 막아 프로그램 흐름을 막고
            
            첫 연결 이후는 계속해서 kqueue 작업이 돌아가야 하니
            
            `accept` 를 한번 수행한 후에 논 블로킹으로 설정한 것이다.
            
            그 다음 클라이언트의 정보를 `Client` 클래스에 저장한다.
            
            저장한 후에 해당 클라이언트 fd에서 일어나는 이벤트를 세팅하고 감지할
            
            `_setEvent` 함수를 호출한다. 이벤트는 `READ` 와 `WRITE` 둘 다 만든다.
            
            주목할 점은 이벤트를 추가하는 `EV_ADD` 플래그는 둘 다 똑같이 있지만
            
            `WRITE`는 이벤트를 활성화 시키지 않는 `EV_DISABLE` 플래그를 주었다.
            
            이유는 kqueue에서 `READ`, `WRITE` 이벤트의 차이점 때문이다.
            
            - **kqueue에서 `READ`, `WRITE` 이벤트의 차이점**
                
                1) **READ 이벤트 (EVFILT_READ)**
                
                - **데이터가 수신될 때 발생.**
                - **데이터가 없으면 이벤트는 발생하지 않음.**
                - 따라서, `READ` 이벤트는 **소켓이 닫히거나 데이터가 들어올 때까지 kqueue에서 대기 상태로 유지**해도 부담이 크지 않습니다.
                - **일반적으로 수동으로 `EV_DISABLE` 하지 않음.**
                
                ### 동작 흐름:
                
                1. 소켓이 논블로킹 모드에서 비어있으면 `read()`가 즉시 `1`을 반환 (`errno == EAGAIN`).
                2. 클라이언트가 데이터를 보내면 `EVFILT_READ` 이벤트가 트리거됨.
                3. 데이터가 도착할 때만 `kevent()`가 트리거되므로, **항상 `EV_ENABLE` 상태를 유지**하는 것이 일반적.
                
                **결론:**
                
                `EVFILT_READ`는 데이터가 도착할 때까지 이벤트를 유지해도 성능에 큰 영향을 미치지 않기 때문에 **명시적으로 `EV_DISABLE`이 자주 필요하지 않음**.
                
                ---
                
                ### (2) **WRITE 이벤트 (EVFILT_WRITE)**
                
                - **버퍼가 가득 차지 않았을 때 발생.**
                - 데이터가 송신되지 않아 송신 버퍼가 가득 찬 경우, `write()`가 실패 (`EAGAIN` 반환).
                - 반대로, 송신 버퍼가 비어 있을 경우 `EVFILT_WRITE` 이벤트가 계속 발생할 수 있음.
                
                ### 동작 흐름:
                
                1. 클라이언트의 송신 버퍼가 비어 있으면 `EVFILT_WRITE`가 계속 트리거됨.
                2. 모든 데이터를 `write()`로 전송한 후에도 이벤트가 계속 발생할 수 있음.
                3. **불필요한 이벤트 발생을 방지하기 위해, 데이터를 다 보낸 후에는 `EV_DISABLE`로 비활성화.**
                
                **결론:**
                
                `EVFILT_WRITE`는 **송신 버퍼가 준비되었을 때마다 이벤트가 계속 트리거될 수 있기 때문에**, 필요할 때만 `EV_ENABLE`하고, 불필요할 때는 `EV_DISABLE`하는 방식이 일반적이다.
                
            
            ```cpp
            void Server::readClient(int fd, const ServConf& conf)
            {
                char buffer[1024] = {0};
            
                // 다 읽을 때 까지 readClient가 계속해서 호출됨
                // kqueue 에서 read 를 다 할때 까지 이벤트를 삭제하지 않음
                ssize_t size = read(fd, buffer, sizeof(buffer) - 1);
                if (size <= 0)
                {
                    // size == 0 -> 클라이언트 쪽에서 소켓을 닫음 (FIN)
                    // 따라서 서버쪽 클라이언트 소켓도 닫음
                    // 닫으면 kqueue 에서 이벤트를 자동으로 삭제
                    closeClient(fd);
                    return ;
                }
                buffer[size] = '\0';
                // read 를 할때마다 메시지에 이어 붙히는 함수
                // *중요* string 매개 변수는 안됨! 바이너리 손상 이슈
                _client[fd].setMessage(buffer, size);
            
                // 요청문을 체크하기 위해 가져옴
                const string& message = _client[fd].getMessage();
                size_t msgSize = _client[fd].getSize(); // 요청문 size
                size_t headerEnd = message.find("\r\n\r\n"); // 헤더가 끝나는 위치
                size_t maxSize = conf.getServBlock(_client[fd].getIndex()).getMaxSize(); // conf 파일의 max_size 가져옴
                if (headerEnd != string::npos) // 헤더가 있다면
                {
                    headerEnd += 4; // "\r\n\r\n" 이후로 자리를 옮김
                    if (headerEnd > 8192) // 헤더가 8192 바이트 이상이면 오류
                        return _sendError(fd, "431", "Request Header Fields Too Large", conf);
                    string header = message.substr(0, headerEnd); // 헤더 추출
                    size_t lenPos = header.find("Content-Length:"); // Content-Length 헤더 확인
                    size_t chunkPos = header.find("Transfer-Encoding: chunked"); // 인코딩 헤더가 있는지 확인
            
                    if (lenPos != string::npos) // Content-Length 헤더가 있다면
                    {
                        size_t start = lenPos + 16; // Content-Length 헤더의 value 시작 위치
                        size_t end = header.find("\r\n", start); // CRLF = 끝
                        size_t bodySize = msgSize - headerEnd; // 바디 사이즈 측정
                        // 문자열 -> 숫자로 변환
                        long contentLength = strtol(header.substr(start, end - start).c_str(), NULL, 10);
            
                        if (static_cast<size_t>(contentLength) > maxSize) // max_size 보다 Content-Length 가 더 크면 오류
                            _sendError(fd, "413", "Request Entity Too Large", conf);
                        // Cotent-Length 보다 body 사이즈가 같거나 크면 쓰기 이벤트 활성화, 거기까지만 유효한 바디
                        // 더 작다면 추가적인 read 수행
                        else if (bodySize >= static_cast<size_t>(contentLength))
                            _setEvent(fd, EVFILT_WRITE, EV_ENABLE);
                    }
                    else if (chunkPos != string::npos) // chunked 헤더가 있다면
                    {
                        string body = message.substr(headerEnd);
                        size_t pos = 0;
            
                        while (pos < body.size()) // body 가 있다면
                        {
                            size_t end = body.find("\r\n", pos); // 끝 확인
                            if (end == string::npos)
                                break ;
            
                            // chunked 문은 size, 본문이 한 줄씩 번갈아 가면서 나옴
                            // 먼저 size 가 나오기 때문에 size 측정
                            long chunkSize = strtol(body.substr(pos, end - pos).c_str(), NULL, 16);
                            pos = end; // pos 를 size 줄 끝으로
            
                            // chunksize == 0 이고 다음 줄이 "\r\n\r\n" 이라면 본문 끝
                            if (chunkSize == 0 && body.substr(pos, 4) == "\r\n\r\n")
                                _setEvent(fd, EVFILT_WRITE, EV_ENABLE); // 쓰기 이벤트 활성화
                            else
                            {
                                // 다음 줄인 본문을 무시하기 위해 pos를 본문 + "\r\n" 까지 옮김
                                pos += chunkSize + 2;
                                if (pos > maxSize) // maxsize 체크
                                    _sendError(fd, "413", "Request Entity Too Large", conf);
                            }
                        }
                    }
                    else
                        _setEvent(fd, EVFILT_WRITE, EV_ENABLE); // 두 헤더가 모두 없어도 쓰기 이벤트 활성화
                }
            
            ```
            
            클라이언트가 보내는 데이터를 `READ` 하는 함수다. ~~상당히 길다~~
            
            1024바이트보다 읽을게 더 있다면 계속 호출되며 메시지를 이어붙힌다.
            
            연결이 종료되었다는 반환값인 0 이 오면 클라이언트 소켓을 닫는다.
            
            여기서 `431` 과  `413` 오류를 확인한다.
            
            헤더의 길이 수를 재서 8192 바이트 이상이면 `431` 오류,
            
            `Content-Length` 헤더를 확인하여 바디 사이즈가 `max_bodysize` 이상이거나
            
            `Transfer-Encoding` 헤더를 확인하여 바디가 chunked 되었다면
            
            chunked 된 바디의 바이트 수를 재고 바디 사이즈가 `max_bodysize` 이상이면
            
            `413` 오류를 전송한다.
            
            `Content-Length` , `Transfer-Encoding` 두 헤더 모두 없어도
            
            `WRITE` 이벤트는 활성화 한다.
            
            ```cpp
            void Server::sendClient(int fd, const ServConf& conf)
            {
                // 여기서 fd 는 쓰기 이벤트가 일어난 클라이언트 소켓임
            
                // 클라이언트 소켓에 대응하는 Client 클래스 탐색
                map<int, Client>::iterator it = _client.find(fd);
                // 찾았고 본문이 비지 않았다면
                if (it != _client.end() && it->second.getMessage() != "")
                {
                    Request req;
                    req.initRequest(it->second.getMessage()); // 요청문 파싱
                    Response res(req, conf, it->second.getIndex()); // 응답 생성
            
                    _printLog(fd, req.getMethod(), req.getUrl(), BLUE);
                    // 응답 write, write 할게 없거나 오류면 close
                    if (write(fd, res.getMessage().c_str(), res.getMessage().size()) <= 0)
                        closeClient(fd);
                    else
                    {
                        // 쓰기를 완료했다면 write 이벤트를 명시적으로 비활성화 해야함
                        // 하지 않으면 계속 쓰기 이벤트 발생
                        _setEvent(fd, EVFILT_WRITE, EV_DISABLE);
                        if (req.chkConnection()) // Connection: Closed 라면 close
                            closeClient(fd);
                        else
                            // Connection: Keep-alive 라면 메시지를 비워서 새로운 메시지 받을 준비
                            it->second.setMessage(NULL, 0);
                    }
                }
            }
            ```
            
            클라이언트에게 보낼 응답을 `WRITE` 하는 함수이다.
            
            받은 요청문을 파싱한 데이터를 기반으로 응답을 생성하고 전송한다.
            
            ```cpp
            
            void Server::closeClient(int fd)
            {
                _printLog(fd, "Disconnected", "", RED);
                close(fd);
                _client.erase(fd);
            }
            ```
            
            클라이언트 소켓 닫는 함수. 해당 클라이언트 정보도 지운다.
            
            ```cpp
            void Server::_printLog(int fd, const string& one, const string& two, const string& color)
            {
                time_t now = time(NULL);
                char* timeInfo = ctime(&now);
                map<int, Client>::iterator it = _client.find(fd);
            
                timeInfo[strlen(timeInfo) - 1] = '\0';
                cout << color << "\r[" << timeInfo << "] " << RESET;
                cout << it->second.getIP() << ":" << it->second.getPort() << " > ";
                cout << one << " " << two << endl;
            }
            ```
            
            로그 출력 함수.
            
            ```cpp
            void Server::checkTimeout(long timeout)
            {
                if (timeout == 0)
                    return ;
            
                time_t now = time(NULL);
                map<int, Client>::iterator it = _client.begin();
                while (it != _client.end())
                {
                    time_t last = it->second.getLastTime();
                    if (now - last > timeout)
                    {
                        _printLog(it->first, "Disconnected", "", RED);
                        close(it->first);
                        it = _client.erase(it);
                    }
                    else
                        ++it;
                }
            }
            ```
            
            타임아웃 체킹 함수. `timeout` 이 지나면 클라이언트 소켓을 닫고 삭제.
            

- **CGI**
    - 과제 조건 분석
        
        웹 서버 프로그램에서 자식 프로세스로 CGI를 실행해야 한다.
        
        `fork()` 이후 `execve()` 로 CGI 프로그램(파이썬 인터프리터)와
        
        CGI 스크립트를 실행한다.
        
        이때 웹 서버 프로세스, CGI 프로세스를 파이프로 연결하여
        
        요청문의 바디는 웹 서버 에서 표준 출력으로 CGI에 전달하고,
        
        CGI 처리 결과물도 표준 출력으로 웹 서버에 전달한다.
        
        다만 요청문의 헤더에 있는 메타 데이터는 환경 변수(`envp`)를 통해 CGI에 전달한다.
        
        1. Because you won’t call the CGI directly, use the full path as PATH_INFO.
        `PATH_INFO`란 `cgi-bin/my_cgi.py/foo/bar` 같은 Url에서 스크립트 명 이후의 경로를
        담기 위한 환경 변수이다. 즉, `PATH_INFO = /foo/bar` 
        직접 CGI 프로그램을 실행하지 않을 것이기 때문에, 이런 환경변수 설정이 필요하다.
        2. Your program should call the CGI with the file requested as first argument.
        CGI 프로그램을 터미널에서 직접 실행하지 않고 웹 서버 안에서 실행할 것이기 때문에,
        `execve()` 함수의 첫 번째 인자로 인터프리터인 `python3` 를 주고,
        두 번째 인자(인터프리터의 첫 번째 인자)로 `my_cgi.py` 를 줘야한다.
        여기서 중요한건 CGI 프로그램 = `python3` 이고,
        CGI 스크립트 = `my_cgi.py` 라는 점.
        세 번째 인자엔 설정한 환경 변수들(`envp` )를 준다.
        
    - CGI 스크립트
        
        ```python
        #!/usr/bin/env python3
        
        import os
        import sys
        import urllib.parse
        import warnings
        warnings.filterwarnings("ignore", category=DeprecationWarning)
        import cgi
        import mimetypes
        import shutil
        
        def chkError(e):
            if "Permission denied" in str(e):
                return "403 Forbidden"
            elif "No such file or directory" in str(e):
                return "404 Not Found"
            else:
                return "500 Internal Server Error"
        
        # 환경 변수에서 각종 정보 읽기
        path_info = os.environ.get("PATH_INFO", "")
        query_string = os.environ.get("QUERY_STRING", "")
        request_method = os.environ.get("REQUEST_METHOD", "")
        content_type   = os.environ.get("CONTENT_TYPE", "")
        content_length = os.environ.get("CONTENT_LENGTH", 0)
        root_dir = os.environ.get("DOCUMENT_ROOT", "")
        
        content_length = int(content_length)
        
        # HTML 제목
        title = ""
        
        # 상태 라인
        status = ""
        
        # CGI 가 접근 가능한 루트 디렉토리 경로 설정
        root_dir = os.path.join(root_dir, "cgi_storage")
        
        # 루트 디렉토리가 없으면 생성
        if not os.path.exists(root_dir):
            os.makedirs(root_dir, exist_ok=True)
           
        # PATH_INFO 의 경로를 추가
        if path_info:
            path = root_dir + path_info
        else:
            path = root_dir
        
        # url에서 파일명, MIME 타입, 확장자 추출
        filename = os.path.basename(path)
        mime_type, encoding = mimetypes.guess_type(path)
        # MIME 타입이 없다면 바이너리로 간주
        if mime_type is None:
            mime_type = "application/x-mach-binary"
        extension = mime_type.split("/")[-1]
        
        # 콘텐츠 타입이 텍스트인지 바이너리인지 확인하기 위한 플래그
        isbinary = False
        
        # 바이너리 타입 리스트
        binary_types = [
                "application/octet-stream",
                "application/x-mach-binary",
                "application/pdf",
                "application/zip",
                "application/x-tar",
                "application/x-gzip",
                "application/vnd.",
                "image/",
                "audio/",
                "video/",
                ]
        
        # 딕셔너리에 쿼리문 파싱 후 담기
        params = urllib.parse.parse_qs(query_string)
        
        # 반환할 바디
        content = ""
        
        # 최종 경로를 접근 할 수 있는지 체크
        absolute_path = os.path.abspath(path)
        if not absolute_path.startswith(root_dir):
            status = _chkError("Permission denied")
        
        # GET 인데 PATH_INFO 가 존재할 경우
        elif request_method == "GET" and path_info:
        
            # MIME 타입 확인
            isbinary = any(mime_type.startswith(bt) for bt in binary_types)
        
            try:
                # 바이너리와 텍스트 구분하여 read
                if isbinary == False:
                    with open(path, "r", encoding="utf-8") as file: # 텍스트 읽기, with은 파일 열고 닫기까지 수행
                        if content_length > 0:
                            content = file.read(content_length)
                        else:
                            content = file.read()
                else:
                    with open(path, "rb") as file: # 바이너리 읽기, 옵션에 b 가 붙으면 바이너리로 수행
                        if content_length > 0:
                            content = file.read(content_length)
                        else:
                            content = file.read()
            except Exception as e:
                # 파일을 open 하는데 실패할 경우
                params["status"] = [f"Error reading file {filename} : {str(e)}"]
                status = chkError(e)
        
        # POST 인데 multipart 인 경우
        elif request_method == "POST" and content_type.startswith("multipart/form-data"):
            # 폼 가져오기
            form = cgi.FieldStorage()
        
            # HTML form 확인 (file)
            if "file" not in form:
                params["status"] = ["HTML form is missing \"file\"."]
                status = "400 Bad Request"
            else:
                file_item = form["file"]
                # 파일 이름이 존재하면 읽어서 저장
                if file_item.filename:
                    save_path = os.path.join(path, file_item.filename)
                    try:
                        with open(save_path, "wb") as f:
                            f.write(file_item.file.read())
                         # 업로드가 완료된 경우
                        params["status"] = [f"File '{file_item.filename}' uploaded successfully."]
                        status = "201 Created"
                    except Exception as e:
                        # 업로드에 실패한 경우
                        params["status"] = [f"Error uploading file {file_item.filename} : {str(e)}"]
                        status = chkError(e)
                else:
                    params["status"] = ["Error uploading file : File has no name."]
                    status = "400 Bad Request"
        
        # POST 인데 multipart 가 아닌 경우
        elif request_method == "POST":
        
            # Content-Type 확인
            isbinary = any(content_type.startswith(bt) for bt in binary_types)
        
            # Content-Length 확인 후 Content-Type 에 따라 body 읽기
            if content_length > 0:
                if isbinary == True:
                    body = sys.stdin.buffer.read(content_length) # 바이너리 읽기
                else:
                    body = sys.stdin.read(content_length) # 텍스트 읽기
            else:
                if isbinary == True:
                    body = sys.stdin.buffer.read()
                else:
                    body = sys.stdin.read()
        
            # 파일 이름, 확장자 정하기
            filename = "test.txt"
            # 이미 있는 파일명이면 바꿔서 지정
            counter = 1
            while os.path.exists(os.path.join(path, filename)):
                filename = f"test_{counter}.txt"
                counter += 1
            save_path = os.path.join(path, filename)
        
            # 파일 쓰기
            try:
                if isbinary == False:
                    with open(save_path, "w") as file:
                        file.write(body)
                else:
                    with open(save_path, "wb") as file:
                        file.write(body)
                params["status"] = [f"File '{filename}' uploaded successfully."]
                status = "201 Created"
            except Exception as e:
                params["status"] = [f"Error uploading file {filename} : {str(e)}"]
                status = chkError(e)
        
        # DELETE는 PATH_INFO 가 존재해야만 작동
        elif request_method == "DELETE" and path_info:
            try:
                if os.path.isdir(path):
                    shutil.rmtree(path)  # 디렉토리 삭제 (비어있지 않아도 삭제)
                    params["status"] = [f"'{filename}' deleted successfully."]
                elif os.path.isfile(path):
                    os.remove(path)  # 파일 삭제
                    params["status"] = [f"'{filename}' deleted successfully."]
                else:
                    params["status"] = [f"Error: '{filename}' does not exist"] # 파일이 존재하지 않음
                    status = "404 Not Found"
            except Exception as e:
                params["status"] = [f"Error deleting '{filename}' : {str(e)}"]
                status = chkError(e)
        elif request_method == "DELETE":
            params["status"] = ["Error: Need path info"]
            status = "400 Bad Request"
        
        # status 가 비어있으면 정상
        if status == "":
            status = "200 OK"
        
        # status 가 정상이라면 제목을 CGI Test 로 설정
        elif status == "201 Crated" or status == "200 OK":
            title = "CGI Test"
        else:
            title = status # 아니라면 status를 제목으로 설정
        
        # content 가 비어있으면 바디 직접 작성
        if content == "":
            mime_type = "text/html"
            content = f"""<!DOCTYPE html>
        <html>
        <head>
            <title>{title}</title>
        </head>
        <body>
            <h1>{params}</h1>
        </body>
        </html>
        """
        
        # HTTP 응답 길이 재기
        if isbinary == True:
            content_length = len(content)
        else:
            content_length = len(content.encode("utf-8"))
        
        #HTTP 응답 출력
        print(f"{status}")  # 상태 라인
        print(f"Content-Type: {mime_type}") # 타입
        print(f"Content-Length: {content_length}") # 길이
        print() # 헤더 끝
        
        print(content, end='') # 바디 출력
        ```
        
    - 마주쳤던 문제 상황들
        - `POST` 메서드에서 어떤 타입을 받을건지?
            
            `multipart form-data` 만? 아니면 다른것들도 받을까?
            
            → 일단 모두 받게 만들었다.
            
            다만 웹 브라우져에서 `POST` 요청을 보낼때 `multipart` 타입으로 와서
            
            실질적으로 `POST` 요청은 `multipart` 타입만 처리하지만,
            
            이외에 것들도 처리해서 브라우저를 사용하지 않을때 디버깅하기 편했다.
            
        
        - 바이너리 파일 업로드 문제
            
            바이너리 파일은 왜 업로드가 안되지..? 업로드 하면 파일이 손상된다.
            
            파싱에서 `getline` 함수로 읽을 때 텍스트 인코딩으로 처리되는건가?
            
            아님 `stringstream` 을 바이너리 모드로 설정?
            
            아님 파이썬 `FieldStorage` 함수를 바이너리 모드로 설정해야하나?
            
            바이너리가 손상되는 이유를 찾았다!
            
            결국 `getline`, `stringstream`, `FieldStorage`의 바이너리 모드 문제가 아니었다.
            
            소켓에서 `read()`를 할때 바이트 단위로 읽고, 읽은 데이터를 `char*`에 그대로 저장하는데
            
            이 `char*` 문자열을 다른 함수의 매개 변수로 넘길 때 `string` 타입으로 받은게 이유였다.
            
            `string` 객체 내부에서 `char *` 문자열을 복사할때 `strlen()`을 호출하는데,
            
            `strlen()`은 `NULL(\0)`까지만 길이를 잰다.
            
            바이너리 코드에는 `‘\0’`값이 들어가 있을 수 있는데, 문자열의 널 처럼
            
            특별한 값이 아니고 데이터 그 자체라 따로 처리를 하면 안된다.
            
            따라서 매개 변수가 받을 때도 `char *`로 받고, 받은 문자열을
            
            다른 `string`에 하나하나 `push_back` 하는 방법으로 데이터를 보존시켰다.
            
            다른 방법으론 `read()` 함수가 읽은 바이트 수를 반환(`readBytes`)하니
            
            `string.append(message, readBytes)`로 `append`함수에
            
            사이즈를 지정해주는 방법도 있다.
            
            따라서 `string`에 바이너리 데이터가 손상되지 않은채로 담겨져 있을때,
            
            `getline()`함수로 개행까지 읽어도 문제가 없다.
            
            뭔가 개행까지 읽어서 줄 단위로 처리하는 함수라
            
            함수 내부에서 텍스트 인코딩 처리를 하며 읽는다고 생각했는데 아니었다.
            
            ```cpp
            std::istream& getline(std::istream& is, std::string& str, char delim = '\n');
            ```
            
            위가 함수 원형인데, 애초에 반환도 파일 스트림 참조인걸 보면 따로 인코딩 처리는 없나보다.
            
        
        - `FieldStorage()` 함수 작동 오류
            
            CGI 파이썬 스크립트에서 `cgi` 객체의 `FieldStorage()` 함수가 안먹혀서 고생을 좀 했다.
            
            무슨 짓을 해도 함수가 정상 작동을 안해서 빈 객체만 반환했는데, 결국 이유를 알아냈다.
            
            스크립트에 메타데이터를 넘기기 위해 서버가 환경 변수를 설정한 `envp` 를
            
            `execve`의 매개변수로 넘겨준다. 이때 환경 변수 이름을 `FieldStrorage()` 함수가
            
            읽을 수 있도록 정해진 이름으로 설정해야 하는데, 이 부분이 문제 였다.
            
            `REQUEST_METHOD` 를 `REQUEST**ED**_METHOD` 로 이름을 잘못 설정하여 못 읽어 
            
            빈 객체를 반환한 것이다.
            
            하나라도 오류가 있다면 `FieldStorage()`는 빈 걸 반환한다고 한다.