# ft_transcendence

설명: 풀 스택 웹 애플리케이션
상태: 완료

**탁구 게임이 작동하는 웹 사이트를 구현해야 한다.**

- 컨테이너 구축
    
    docker run -it -v $(pwd):/app -w /app node:18 bash (-p 8080:8080 → 사이트 호스팅)
    
    npm init -y
    npm install -D tailwindcss postcss autoprefixer
    npx tailwindcss init -p
    
    echo "@tailwind base; @tailwind components; @tailwind utilities;" > style.css
    
    npm install -g vite
    
    npm install -g typescript
    
    cp vite.config.ts → host 0.0.0.0 수정
    
    npm dev run
    

- 디렉토리 구조
    
    📂 프로젝트 폴더
    │── 📂 dist/                # TypeScript 컴파일 후 생성되는 JavaScript 파일 (출력 폴더)
    │── 📂 src/                 # 소스 코드 (TypeScript + HTML + CSS)
    │   │── 📂 pages/           # 각 페이지 (로딩, 로그인, 게임, 설정 등)
    │   │   │── loading.ts      # 로딩 화면
    │   │   │── login.ts        # 로그인 화면
    │   │   │── game.ts         # 게임 화면
    │   │   │── settings.ts     # 설정 화면
    │   │── 📂 styles/          # Tailwind CSS 관련 파일
    │   │   │── styles.css      # Tailwind CSS 파일 (직접 관리하는 경우)
    │   │── app.ts              # 라우터 및 앱 초기화
    │── 📂 public/              # 정적 파일 (index.html, 이미지 등)
    │   │── index.html          # 메인 HTML 파일
    │── 📂 node_modules/        # npm 패키지 폴더
    │── 📄 tailwind.config.js   # Tailwind CSS 설정 파일
    │── 📄 tsconfig.json        # TypeScript 설정 파일
    │── 📄 package.json         # npm 설정 파일
    │── 📄 [README.md](http://readme.md/)            # 프로젝트 설명
    

- 타입스크립트 → 자바 스크립트 컴파일?
    
    ### 📌 `dist/app.js`의 역할
    
    `dist/app.js`에는 **타입스크립트 파일을 컴파일한 자바스크립트 파일**이 들어 있어.
    
    ---
    
    ### ✅ TypeScript 프로젝트에서 `dist/app.js`가 생성되는 과정
    
    1. **타입스크립트 파일(`src/*.ts`)을 작성**
        
        → 예: `src/router.ts`, `src/loading.ts`, `src/login.ts`
        
    2. **TypeScript 컴파일 (`tsc` 실행)**
        - TypeScript는 `tsconfig.json`의 설정을 따라 `src/*.ts` 파일을 `dist/*.js`로 변환
        - 예를 들어 `src/app.ts` → `dist/app.js`로 컴파일됨
    3. **`dist/app.js`가 계속 덮어쓰기 됨**
        - `tsc --watch` 또는 `npm run build`를 실행하면`tsc`가 **변경된 `src/*.ts` 파일을 감지하고 다시 컴파일**하여 `dist/*.js`를 업데이트함.
        - 즉, `src/app.ts`를 수정하면 **컴파일 후 `dist/app.js`가 새롭게 생성됨**.
    
    ---
    
    ### 📌 `router.ts`가 계속해서 `app`을 업데이트하는가?
    
    아니, **라우터는 TypeScript를 다시 컴파일하지 않고**,
    
    **이미 컴파일된 JavaScript(`dist/app.js`)를 실행하여 `document.getElementById("app")`을 변경하는 역할**을 해.
    
    즉, **라우터가 직접 `dist/app.js`를 덮어쓰지는 않음**.
    
    대신, **`tsc`가 `dist/app.js`를 덮어씌움**.
    
    ---
    
    ### ✅ 정리
    
    - `dist/app.js`는 **TypeScript(`src/app.ts`)를 컴파일한 결과물**.
    - **라우터는 `document.getElementById("app")`을 업데이트할 뿐, `dist/app.js`를 수정하지 않음**.
    - **`tsc --watch`를 실행하면 TypeScript 파일이 변경될 때마다 자동으로 `dist/*.js`가 업데이트됨**.
    - `dist/app.js`를 직접 수정하는 건 의미가 없고, **`src/*.ts`를 수정한 후 다시 컴파일해야 함**. 🚀
    
    ### `router.ts`가 `index.html`에 내용을 전달하는 방식
    
    ### 1️⃣ `index.html`의 구조 (`#app`이 중요한 역할)
    
    ```html
    
    <body class="bg-gray-900 text-white flex justify-center items-center h-screen">
        <!-- 여기에 페이지가 동적으로 삽입됨 -->
        <div id="app"></div>
    
        <!-- 🔥 app.js 실행 -->
        <script type="module" src="dist/app.js"></script>
    </body>
    
    ```
    
    - `<div id="app"></div>`: **라우터가 이 요소의 내용을 변경하며 화면을 업데이트**함.
    - `<script type="module" src="dist/app.js"></script>`: **컴파일된 JavaScript 파일을 실행하여 라우팅을 담당하는 코드(`router.ts`)를 불러옴**.
    
    ---
    
    ### 2️⃣ `router.ts`가 `index.html`과 상호작용하는 방식
    
    ```tsx
    public render() {
        const path = window.location.pathname;
        document.getElementById("app")!.innerHTML = this.routes[path] || "<h1>404 Not Found</h1>";
    }
    ```
    
    - `render()` 함수는 현재 URL을 확인하고, **해당하는 페이지를 `#app` 안에 삽입**함.
    - 예를 들어 `/login`이라면:
        
        ```tsx
        document.getElementById("app")!.innerHTML = loginPage;
        ```
        
        - `loginPage`는 `login.ts`에서 HTML 문자열로 정의되어 있음.
        - 따라서 `#app` 내부가 **로그인 페이지로 변경**됨.
    
    ---
    
    ### 3️⃣ `navigate("/login")` 호출 시 발생하는 일
    
    1. `history.pushState(null, "", "/login")` → URL이 `/login`으로 변경됨.
    2. `this.render();` 실행됨.
    3. `document.getElementById("app")!.innerHTML = loginPage;` → `#app`에 로그인 페이지 삽입.
    4. `setupLogin(this);` 실행 → 버튼 이벤트 리스너 등록됨.
    
    ---
    
    ### ✅ 정리
    
    ✔ `index.html`의 `#app` 요소가 **라우터가 삽입하는 동적 콘텐츠를 담는 컨테이너** 역할을 함.
    
    ✔ `router.ts`의 `render()` 함수가 현재 URL에 맞는 페이지를 찾아 **`#app`에 HTML을 넣어 화면을 업데이트**함.
    
    ✔ `router.navigate("/somePage")`를 실행하면 **URL이 변경되고, `render()`를 호출하여 화면을 갱신**함. 🚀
    

- `pushstate, popstate`
    
    ## 📌 `window.addEventListener("popstate", ...)` 와 `history.pushState`의 역할
    
    ✅ 이 둘은 **브라우저의 히스토리(뒤로 가기/앞으로 가기)를 제어하고 감지하는 기능**을 함.
    
    ✅ **SPA(Single Page Application)에서 URL을 변경하면서도 전체 페이지를 새로고침하지 않고 동작하도록 해줌.**
    
    ---
    
    ## **1️⃣ `history.pushState`란?**
    
    ```tsx
    history.pushState(state, title, url);
    ```
    
    ✅ **현재 페이지의 히스토리를 변경하면서도 페이지 리로드 없이 URL을 바꿈.**
    
    ✅ **새로운 상태(state)를 추가하여 "뒤로 가기"가 가능하게 만듦.**
    
    ✅ **브라우저 주소창의 URL은 변경되지만, 서버에 요청을 보내지 않음.**
    
    ---
    
    ### **🚀 `history.pushState` 예제**
    
    ```tsx
    document.getElementById("btn")!.addEventListener("click", () => {
        history.pushState({ page: 2 }, "Page 2", "/page2");
        console.log("📌 URL 변경됨:", window.location.pathname);
    });
    ```
    
    ### **🔹 실행 결과**
    
    1. 버튼 클릭 전 URL: `http://example.com`
    2. 버튼 클릭 후 URL: `http://example.com/page2` (✅ **페이지 새로고침 없음!**)
    3. 이후 **브라우저의 "뒤로 가기" 버튼을 누르면 `/`로 돌아감.**
    
    ---
    
    ## **2️⃣ `window.addEventListener("popstate", ...)`란?**
    
    ```tsx
    window.addEventListener("popstate", (event) => {
        console.log("📌 popstate 이벤트 발생!", event.state);
    });
    ```
    
    ✅ **"뒤로 가기" 또는 "앞으로 가기"를 눌렀을 때 실행되는 이벤트 리스너.**
    
    ✅ `history.pushState()`로 추가한 상태(`state`)를 감지하여 원하는 동작을 수행할 수 있음.
    
    ---
    
    ### **🚀 `popstate` 이벤트 예제**
    
    ```tsx
    // URL 변경
    history.pushState({ page: 1 }, "Page 1", "/page1");
    history.pushState({ page: 2 }, "Page 2", "/page2");
    
    // 🔥 "뒤로 가기" 버튼을 눌렀을 때 실행됨
    window.addEventListener("popstate", (event) => {
        console.log("📌 뒤로 가기 또는 앞으로 가기 감지됨! state:", event.state);
    });
    ```
    
    ### **🔹 실행 흐름**
    
    1. 처음 URL → `/`
    2. `history.pushState`로 `/page1` 추가
    3. `history.pushState`로 `/page2` 추가
    4. **"뒤로 가기" 버튼 클릭 → `popstate` 이벤트 발생 → 콘솔에 `{ page: 1 }` 출력**
    5. **"앞으로 가기" 버튼 클릭 → `popstate` 이벤트 발생 → 콘솔에 `{ page: 2 }` 출력**
    
    ---
    
    ## **3️⃣ `history.pushState` + `popstate`를 이용한 SPA 라우팅**
    
    ### **✅ `router.ts`에서 URL 변경 시 `render()`를 실행하도록 하는 코드**
    
    ```tsx
    class Router {
        private routes: { [key: string]: string };
    
        constructor(routes: { [key: string]: string }) {
            this.routes = routes;
    
            // 🔥 브라우저의 "뒤로 가기" 또는 "앞으로 가기"를 감지
            window.addEventListener("popstate", () => this.render());
        }
    
        public navigate(url: string) {
            history.pushState(null, "", url);  // 🔥 URL 변경
            this.render();  // 🔥 URL 변경 후 즉시 새로운 화면을 렌더링
        }
    
        public render() {
            const path = window.location.pathname;
            document.getElementById("app")!.innerHTML = this.routes[path] || "<h1>404 Not Found</h1>";
        }
    }
    
    // 📌 라우터 실행
    const routes = {
        "/": "<h1>Home Page</h1>",
        "/login": "<h1>Login Page</h1>",
        "/home": "<h1>Welcome Home</h1>",
    };
    
    const router = new Router(routes);
    router.render();
    
    // 🔥 버튼 클릭으로 페이지 이동 테스트
    document.getElementById("btn-login")!.addEventListener("click", () => router.navigate("/login"));
    document.getElementById("btn-home")!.addEventListener("click", () => router.navigate("/home"));
    ```
    
    ### **🔹 실행 흐름**
    
    1. 처음 URL: `/` → `render()` 실행 → `"Home Page"` 표시
    2. 버튼 클릭(`navigate("/login")`) → URL이 `/login`으로 변경 → `"Login Page"` 표시
    3. 버튼 클릭(`navigate("/home")`) → URL이 `/home`으로 변경 → `"Welcome Home"` 표시
    4. 🔥 **브라우저 "뒤로 가기" 버튼 클릭**
        - URL이 `/login`으로 변경 → `"Login Page"` 표시 (popstate 이벤트 실행)
    5. 🔥 **브라우저 "앞으로 가기" 버튼 클릭**
        - URL이 `/home`으로 변경 → `"Welcome Home"` 표시
    
    ---
    
    ## **🎯 결론**
    
    ✅ `history.pushState()` → **URL을 변경하지만, 페이지 새로고침 없이 동작 가능**
    
    ✅ `window.addEventListener("popstate", ...)` → **뒤로 가기/앞으로 가기 감지 가능**
    
    ✅ 둘을 조합하면 **SPA(Single Page Application) 방식으로 동작하는 라우터 구현 가능** 🚀
    
    💡 **즉, `pushState()`로 URL을 변경하고, `popstate`를 감지하여 계속 `render()`를 실행하면 "새로고침 없는 라우팅"을 구현할 수 있음!** 🎯
    

- 어떻게 `router.ts` 프로그램이 안죽고 살아있는가?
    
    ### **📌 이벤트 기반 프로그램이 종료되지 않는 이유**
    
    ✅ **JavaScript는 C/C++과 다르게 "이벤트 루프(Event Loop)"를 사용함.**
    
    ✅ **프로그램이 끝나지 않는 이유는 `window.addEventListener("popstate", ...)` 같은 이벤트 리스너가 계속 살아있기 때문.**
    
    ✅ **이벤트 리스너 자체가 블로킹(blocking)되는 것은 아님. 하지만 이벤트 루프가 프로그램을 계속 실행 상태로 유지함.**
    
    ---
    
    ## **1️⃣ C/C++과 JavaScript의 실행 방식 차이**
    
    ### **✅ C/C++ (동기 실행)**
    
    ```cpp
    #include <iostream>int main() {
        std::cout << "Hello, World!" << std::endl;
        return 0;  // 💀 프로그램 종료됨
    }
    
    ```
    
    - **C/C++은 실행이 끝나면 메인 함수(`main()`)가 종료되고, 프로그램이 즉시 종료됨.**
    - **이벤트 루프 같은 개념이 없어서, 무한 루프(`while (true) {}`)를 직접 만들어야 함.**
    - **즉, `main()`이 끝나면 OS가 프로그램을 종료함.**
    
    ---
    
    ### **✅ JavaScript (이벤트 루프 사용)**
    
    ```tsx
    window.addEventListener("popstate", () => {
        console.log("📌 popstate 이벤트 감지됨!");
    });
    console.log("🚀 프로그램 실행 중...");
    ```
    
    - **여기서 `console.log("🚀 프로그램 실행 중...");`이 먼저 실행됨.**
    - **하지만 `window.addEventListener("popstate", ...)`는 이벤트 리스너를 등록하고 끝남.**
    - **이벤트 리스너가 등록되어 있기 때문에 브라우저의 이벤트 루프가 프로그램을 종료하지 않음.**
    
    ---
    
    ## **2️⃣ JavaScript에서 프로그램이 종료되지 않는 이유**
    
    ### **✅ JavaScript는 싱글 스레드 + 이벤트 루프 기반**
    
    - JavaScript는 기본적으로 **"한 번 실행하고 끝나는" 언어**가 아님.
    - 대신 **이벤트 루프(Event Loop)**라는 개념을 사용해서 **비동기 이벤트(클릭, 네트워크 요청, 타이머 등)를 기다리면서 계속 실행 상태를 유지함.**
    
    ---
    
    ### **🚀 이벤트 루프 동작 방식**
    
    1. **JavaScript 코드가 실행됨**
        - `console.log("🚀 프로그램 실행 중...");` 같은 동기 코드가 실행됨.
    2. **이벤트 리스너 등록 (`window.addEventListener`)**
        - `popstate` 이벤트가 감지되면 실행할 함수를 등록해둠.
    3. **프로그램이 끝나지 않고 대기 상태로 유지됨**
        - 이벤트 루프가 **이벤트가 발생할 때까지 기다림**.
        - 프로그램이 종료되지 않고 **대기 상태(idle)로 남아 있음.**
    4. **사용자가 "뒤로 가기" 버튼을 누르면 `popstate` 이벤트 발생**
        - *이벤트 큐(Event Queue)**에 `popstate` 이벤트가 추가됨.
        - *이벤트 루프(Event Loop)**가 큐에서 이벤트를 꺼내서 실행함.

- 이벤트는 어떻게 처리하나? 이벤트 리스너 등록
    
    ### **📌 이벤트 리스너만 등록하는 경우, 반복문으로 모든 함수를 순회해도 괜찮다!**
    
    ✅ **이벤트 리스너를 등록하는 함수만 `pageFuncs` 배열에 들어간다면, `forEach()`를 사용해도 문제가 없다.**
    
    ✅ **버튼이 5개라면, `forEach()`를 통해 5개의 이벤트 리스너가 등록되고, 각각 클릭될 때 실행된다.**
    
    ✅ 즉, **반복문이 실행되더라도 즉시 실행되는 게 아니라 "버튼이 클릭될 때"만 동작하므로 안전하다!**
    

모든 페이지에 사이드 바를 직접 넣을 것인가?
이러면 라우터로 모든게 컨트롤 가능하지만 버튼 클릭시 페이지 전체를 항상 렌더링해야함.

→ 이걸로 할듯. 어차피 사이드바를 제외하곤 항상 새로 렌더링 하는데
사이드바 렌더링이 코스트가 큰것도 아니라서.

아니면 홈페이지에서 동적으로 가져올 것인가?
이러면 라우터 하나가 아니라 두개로 하거나 다른 방식으로 해야해서 복잡함.
또한 앞으로 가기 뒤로 가기가 버튼 단위로 안됨.

vite build는 번들링이기 때문에 모든 세팅이 끝난 다음에 돌려야 한다. 

- 백, 프론트 라우팅에 관하여
    
    `/profile` ,`/otp` 등 라우팅을 백과 프론트 에서 할때 라우트 이름이 곂치면 안된다.
    
    예를 들어 `/profile`이 곂친 상태에서 프론트 라우터를 무시하고 브라우저에 url을 직접 입력하면
    
    현재 앱이 백의 fastify 서버에서 돌아가기 때문에 백의 `/profile` 라우트에서
    
    프로필 데이터를 json 파일로 `send`한다.
    
    이 때문에 브라우저는 json 프로필 데이터만 텍스트로 표시하고 프론트 페이지는 표시하지 않는다.
    
    따라서 바람직한 방법은 라우트 이름을 다르게하고, (ex) `/profile` → `/sendprofile`)
    
    프론트에서 백의 라우트를 호출하여 백에서 `send`한 정보를 `fetch` 하여 가공시킨 다음
    
    프론트 페이지에 띄우는 것이다.
    

닉네임, 프로필 사진 모두 받아야 업로드 되게 하자. 그래야 편할듯.

페이지엔 무조건 대문자로 표시되는데 소문자 대문자 섞어서 똑같은 알파벳 하면 프로필이 생성된다.

이러면 곂치는거 같다.

OTP 페이지는 구글 로그인이 되야만 접근하게 했고,

게임, 프로필 수정, 대시보드 페이지는 JWT 토큰이 있어야 접근 가능하게 했다.

OTP 인증은 한번만 하면 다음엔 하지 않도록 만들었고,

로그아웃을 하면 JWT토큰이 파기되어 다시 OTP 인증을 해야 한다.

JWT토큰이 파기되면 프로필 정보도 사라져 닉네임, 프로필 사진, 경기 기록 등이 초기화된다.

→ 이 문제는 단순히 백엔드 sql 명령문을 `DELETE` 로 해서 생긴 문제이다.
     `UPDATE`로 바꾸니 해결!

- Node.js 스트리밍 I/O 응답 차이
    
    ## ✅ 질문 요약
    
    > ❓ “에러 상황에서는 스트림 소비가 느려서 응답도 늦게 오는데,
    > 
    > 
    > **정상 상황에서는 왜 그렇게 빠르게 처리되죠?**
    > 
    > 같은 스트림을 읽는 거 아닌가요?”
    > 
    
    ---
    
    ## ✅ 간단한 결론부터
    
    > 정상 상황에서는 스트림이 '읽기 + 저장' 작업이 동시에 잘 동작하기 때문에 느려 보이지 않고,
    > 
    > 
    > 에러 상황에서는 스트림을 **"읽기만 하되 아무것도 하지 않아서" 처리 속도가 느려 보이는 겁니다.**
    > 
    
    ## ✅ 질문 요약
    
    > ❓ resume()을 쓰니까 스트림 처리가 빨라지는데, 안 쓰면 왜 오래 걸리냐?
    > 
    > 
    > 심지어 파일을 안 저장하더라도 그냥 `resume()` 한 줄 차이인데,
    > 
    > 이게 응답 지연에 그렇게 영향을 주는 이유가 뭐냐?
    > 
    
    ---
    
    ## 🔥 결론 먼저
    
    > Node.js 스트림은 기본적으로 "lazy(지연 소비)" 방식이라
    > 
    > 
    > **스트림을 `.resume()`하거나 `.pipe()`하지 않으면 데이터를 안 읽습니다.**
    > 
    > 그래서 안 읽으면 **스트림이 열려 있는 상태에서 요청이 끝나지 않아요.**
    > 
    
    ---
    
    ## 💡 비유로 설명
    
    > 브라우저: “파일 보낼게요~!”
    > 
    > 
    > 서버: (듣고만 있음…)
    > 
    > 브라우저: “여보세요? 왜 아무 반응이 없지…?” (연결 유지)
    > 
    > 서버: (응답 안함, 그냥 가만히 있음)
    > 
    > 브라우저: `fetch("/locales/en.json")` 하려는데 → 아직 연결 안 닫힘 → `pending`
    > 
    
    ---
    
    ## 🧠 내부 동작 설명 (Node.js 스트림 시스템)
    
    ### ✅ Node.js의 스트림 기본 원칙
    
    - `Readable` 스트림(파일 업로드 등)은 **데이터가 push되지만**,
        
        소비 코드가 없으면 → **push된 데이터는 버퍼에 쌓입니다.**
        
    - 소비를 유도하려면:
        - `.pipe()`
        - `.on('data', ...)`
        - `.resume()`
    - **이 중 아무것도 안 하면:**
        
        스트림은 **pause 상태** → 브라우저 입장에서는 **"요청이 아직 처리 중"**
        
        ## 🔥 핵심 차이
        
        | 코드 | 동작 |
        | --- | --- |
        | `for await (const part of parts) {}` | 스트림 자체를 순회하지만 **실제로 읽지 않음** |
        | `for await (const part of parts) { part.file.resume(); }` | 스트림을 소비해서 **버퍼가 비워짐** → 연결 닫힘 가능 |
        
        ## ✅ 왜 resume() 한 줄로 해결되는가?
        
        - **`resume()`은 Node.js에서 `pause()`된 스트림을 강제로 읽게 만드는 함수입니다.**
        - 이걸 호출하지 않으면 **Node.js는 그 스트림을 '아직 처리 중인 상태'로 간주**해서
            - `req` 객체도 열려 있고
            - Fastify는 요청이 완료되지 않았다고 판단
        - **그 결과 `reply.send()`를 호출해도 응답이 실제로 완료되지 않고 지연**
    
    ---
    

- 스트림을 소비하지 않는데 왜 파일 크기에 비례하여 펜딩 시간이 정해지는가?
    
    ## ✅ 질문 요약
    
    > ❓ 스트림을 소비하지 않으면 문제가 생기는 건 맞는데,
    > 
    > 
    > 왜 **작은 파일은 빨리 펜딩이 끝나고**, **큰 파일은 오래 걸리는가?**
    > 
    > 스트림을 안 읽는 건 똑같은데 왜 시간 차이가 나는 거지?
    > 
    
    ---
    
    ## 🔥 정답 먼저
    
    > 브라우저는 업로드 중인 파일을 서버가 받아주지 않으면 데이터를 천천히 밀어넣습니다.
    > 
    > 
    > 그리고 그 속도는 **파일 크기에 비례해서 오래 걸립니다.**
    > 
    
    ---
    
    ## 🧠 이게 무슨 말이냐면?
    
    ### 🔹 Node.js 서버가 스트림을 소비하지 않으면 (= `resume()` 안 하면)
    
    - 브라우저는 TCP 연결을 통해 파일 데이터를 보내려다가
    - **"상대방(서버)이 데이터를 읽지 않고 있다"는 신호를 감지**함
    - 그러면 **브라우저는 일종의 흐름 제어(Flow Control)** 를 시작해서
        
        데이터를 아주 천천히, 조금씩 보내기 시작해요
        
    
    ---
    
    ## 📦 파일 크기와 지연의 관계
    
    | 파일 크기 | 브라우저가 보내야 할 데이터 양 | 서버가 소비하지 않음 | → 전송 시간 |
    | --- | --- | --- | --- |
    | 작음 (100KB) | 금방 다 push됨 | 다 버퍼에 들어감 | 응답 빨리 도착 |
    | 큼 (700KB~1MB) | 한 번에 다 못 보냄 | 브라우저가 전송 속도 늦춤 | ❗ 응답 지연 발생 |
    
    > 브라우저는 서버의 소비 속도에 맞춰서 데이터를 천천히 밀어넣기 때문에,
    > 
    > 
    > **스트림을 소비하지 않을 경우, 파일이 클수록 전송 완료까지 오래 걸리는 것**이에요.
    > 
    
    ---
    
    ## 🔬 내부적으로 어떤 일이 벌어지냐면?
    
    - Node.js는 요청의 `Content-Length`만 보고 스트림을 시작하지만
    - `for await (part of parts)` 내부에서 `part.file.resume()`을 호출하지 않으면
    → **TCP 소켓의 버퍼가 가득 차도 읽지 않음**
    - 이때 브라우저는 **TCP의 윈도우 크기(수신 버퍼 한계)에 따라 전송 속도를 줄임**
    - 큰 파일일수록 → 전송 다 완료하는 데 더 오래 걸림
    - 그래서 **브라우저 입장에선 '요청이 아직 끝나지 않았다'고 판단 → 다음 요청(pending)**