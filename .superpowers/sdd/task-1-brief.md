### Task 1: panzer4 스캐폴드 복사 및 프로젝트 이름 정리

**Files:**
- Create/overwrite: `CMakeLists.txt`, `.gitignore`, `LICENSE`, `sdkconfig.defaults`, `dependencies.lock` (있으면)
- Create: `main/main.c`, `main/CMakeLists.txt` (임시로 panzer4 내용)
- Create: `components/bluepad32/`, `btstack/`, `cmd_nvs/`, `cmd_nvs_4.4/`, `cmd_system/`, `cmd_system_4.4/`
- Do not copy: `build/`, `cmake-build-*/`, `sdkconfig`, `sdkconfig.old`, `components/rctank/` (Task 2에서 새로 작성)
- Keep: `env.bat`, `AGENTS.md`, `docs/`

**Interfaces:**
- Produces: `idf.py set-target esp32` 후 configure 가능한 트리 (main은 아직 rctank 없이 컴파일 실패 가능 → Task 2에서 rccar로 연결)

- [ ] **Step 1: 제외 목록으로 컴포넌트/메인 복사**

PowerShell (프로젝트 루트 `nutcracker-idf`에서):

```powershell
$src = "C:\Users\painnick\Documents\Projects\panzer4-idf"
$dst = "C:\Users\painnick\Documents\Projects\nutcracker-idf"

Copy-Item "$src\.gitignore" $dst -Force
Copy-Item "$src\LICENSE" $dst -Force -ErrorAction SilentlyContinue
Copy-Item "$src\sdkconfig.defaults" $dst -Force
Copy-Item "$src\dependencies.lock" $dst -Force -ErrorAction SilentlyContinue
Copy-Item "$src\CMakeLists.txt" $dst -Force

New-Item -ItemType Directory -Force -Path "$dst\main" | Out-Null
Copy-Item "$src\main\main.c" "$dst\main\" -Force
Copy-Item "$src\main\CMakeLists.txt" "$dst\main\" -Force
# my_platform 은 Task 6에서 카용으로 새로 작성. 임시로 복사해 두되 rccar 미완성이면 이후 교체
Copy-Item "$src\main\my_flatform.c" "$dst\main\my_platform.c" -Force

foreach ($c in @("bluepad32","btstack","cmd_nvs","cmd_nvs_4.4","cmd_system","cmd_system_4.4")) {
  robocopy "$src\components\$c" "$dst\components\$c" /E /NFL /NDL /NJH /NJS /nc /ns /np
}
```

주의: panzer4 파일명은 `my_flatform.c`(오타)이다. nutcracker에서는 `my_platform.c`로 저장한다.

- [ ] **Step 2: 루트 CMake 프로젝트명 변경**

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.13)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nutcracker_idf)
```

- [ ] **Step 3: main CMakeLists가 my_platform.c를 가리키게 수정**

`main/CMakeLists.txt`:

```cmake
set(srcs
        "main.c"
        "my_platform.c")

set(requires "bluepad32" "btstack" "rccar")

idf_component_register(SRCS "${srcs}"
        INCLUDE_DIRS "."
        REQUIRES "${requires}")
```

이 시점에는 `rccar`가 없어 빌드 실패가 정상이다. Task 2에서 최소 스텁으로 통과시킨다.

- [ ] **Step 4: 커밋**

```bash
git add -A
git commit -m "chore: panzer4 기반 스캐폴드 복사 및 프로젝트명 정리"
```

---

