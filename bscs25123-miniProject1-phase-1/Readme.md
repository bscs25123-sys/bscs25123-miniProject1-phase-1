
ON 5/10/26 WROTE THESE FUNCS:-
diff --git a/bscs25123-miniProject1-phase-1.sln b/bscs25123-miniProject1-phase-1.sln
new file mode 100644
index 0000000..8efd9dd
--- /dev/null
+++ b/bscs25123-miniProject1-phase-1.sln
@@ -0,0 +1,31 @@
+﻿
+Microsoft Visual Studio Solution File, Format Version 12.00
+# Visual Studio Version 17
+VisualStudioVersion = 17.14.36915.13 d17.14
+MinimumVisualStudioVersion = 10.0.40219.1
+Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "bscs25123-miniProject1-phase-1", "bscs25123-miniProject1-phase-1\bscs25123-miniProject1-phase-1.vcxproj", "{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}"
+EndProject
+Global
+	GlobalSection(SolutionConfigurationPlatforms) = preSolution
+		Debug|x64 = Debug|x64
+		Debug|x86 = Debug|x86
+		Release|x64 = Release|x64
+		Release|x86 = Release|x86
+	EndGlobalSection
+	GlobalSection(ProjectConfigurationPlatforms) = postSolution
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Debug|x64.ActiveCfg = Debug|x64
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Debug|x64.Build.0 = Debug|x64
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Debug|x86.ActiveCfg = Debug|Win32
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Debug|x86.Build.0 = Debug|Win32
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Release|x64.ActiveCfg = Release|x64
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Release|x64.Build.0 = Release|x64
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Release|x86.ActiveCfg = Release|Win32
+		{1995B1E0-A77C-45B4-901E-02D4ABA7B4D9}.Release|x86.Build.0 = Release|Win32
+	EndGlobalSection
+	GlobalSection(SolutionProperties) = preSolution
+		HideSolutionNode = FALSE
+	EndGlobalSection
+	GlobalSection(ExtensibilityGlobals) = postSolution
+		SolutionGuid = {07E7D4CC-15F0-4309-9E1D-DF5BBF6E7E68}
+	EndGlobalSection
+EndGlobal
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
new file mode 100644
index 0000000..6116db2
--- /dev/null
+++ b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
@@ -0,0 +1,319 @@
+
+// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================
+
+// Pipeline this file implements, top to bottom:
+//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
+//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
+//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
+//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
+//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)
+
+
+#include <iostream>
+#include <string>
+#include <cstdint>
+#include <fstream>
+#include <cstdio>
+using namespace std;
+
+// ---- Constants ----
+const int32_t MAX_VARS_PER_FRAME = 16;
+const int32_t MAX_STACK_DEPTH = 64;
+const int32_t MAX_FUNCS = 128;
+const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
+const int32_t MAX_PATCHES = MAX_FUNCS * 4;
+const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
+const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
+const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever
+
+// ---- Custom data structures
+
+// Stack: back the live Call Stack during execution
+template <typename T>
+class Stack
+{
+    struct Node
+    {
+        T data;
+        Node* next;
+    };
+    Node* top;
+    int32_t count;
+
+public:
+    // Implement these functions:
+    Stack()
+    { // initialize the stack
+    }
+    void push(const T& val)
+    {
+
+        // pushes the value on the stack if max limit is not reached yet.
+    }
+    T pop()
+    {
+        // pop the top value on the stack
+    }
+    T& peek()
+    {
+        // returns the top value on the stack
+    }
+    bool isEmpty()
+    {
+    }
+    int32_t depth()
+    {
+    }
+    int32_t snapshot_into(T out[], int32_t maxLen)
+    {
+        // copies every frame, top to bottom in the array given as a parameter
+        // this is what buildSnapshot() call, returns count written
+    }
+};
+
+
+// Timeline : doubly linked list of Snapshots
+struct Snapshot; // fwd declaration;
+struct TimelineNode
+{
+    Snapshot* data;
+    TimelineNode* next;
+    TimelineNode* prev;
+};
+class Timeline
+{
+    TimelineNode* head, * tail;
+    int32_t stepCount;
+
+public:
+    // Implement these functions
+    Timeline()
+    {
+    }
+    void record(Snapshot* s)
+    {
+        // add record in the timeline
+    }
+    TimelineNode* begin()
+    {
+    }
+    int32_t getStepCount()
+    {
+    }
+};
+
+// Core structs
+struct Variable
+{
+    string name;
+    int32_t value;
+};
+struct Frame
+{
+    string func_name;
+    int32_t argc;
+    Variable argv[MAX_VARS_PER_FRAME];
+    int32_t returnLine;
+    Variable locals[MAX_VARS_PER_FRAME];
+    int32_t localCount;
+};
+struct Snapshot
+{
+    Frame callStack[MAX_STACK_DEPTH];
+    int32_t stackDepth;
+};
+struct TTDBHeader
+{
+    char magic[4]; // "TTDB"
+    int32_t version;
+    int32_t stepCount;
+    int64_t indexOffset;
+};
+void writeHeader(FILE* f, const TTDBHeader& h)
+{
+    fwrite(h.magic, 1, 4, f);
+    fwrite(&h.version, sizeof(int32_t), 1, f);
+
+    // placeholder for other two data members
+}
+
+// resolve.bin - bookkeeping
+struct FuncEntry
+{
+    string funcName;
+    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
+};
+struct PendingPatch
+{
+    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
+    string targetFuncName;
+};
+
+
+
+// PASS 0x0: READING source.bin + VALIDITY CHECK
+bool readSourceLine(ifstream& in, string& out)
+{
+    char rd;
+    while (true) {
+        out.clear();
+        bool found = false;
+        while (in.read(&rd, 1)) {
+            found = true;
+            if (rd == '\n') {
+                break;
+            }
+            if (rd == '\r') {
+                continue;
+            }
+            out += rd;
+        }
+        bool blank = true;
+        for (char c : out) {
+            if (c != ' ' and c != '\t') {
+                blank = false;
+                break;
+            }
+        }
+        if (!blank)
+            return true;
+        if (!found)
+            return false;
+    }
+}
+string firstWord(const string& line)
+{
+    string firstword="";
+    for (int i = 0; line[i] != ' ' and i<line.size(); i++) {
+        firstword += line[i];
+  }
+    return firstword;
+}
+string secondWord(const string& line)
+{
+    string fst = firstWord(line);
+    while(fst=="func") {
+        continue;
+    }
+    string secword = " ";
+    for(int i=0;i<line.size() and line[i]!='\0';i++){
+        secword += line[i];
+    }
+    return secword;
+}
+bool validateProgram(const char* sourcePath)
+{
+    ifstream in(sourcePath, ios::binary);
+    if (!in) {
+        return false;
+    }
+    bool inside = false;
+    string line;
+    while (readSourceLine(in, line)) {
+        string frstwrd = firstWord(line);
+        if (frstwrd == "func") {
+            if (inside)
+                return false;
+            inside = true;
+            continue;
+        }
+        if (frstwrd == "func_end") {
+            if(!inside)
+            return false;
+            inside = false;
+            continue;
+        }
+    }
+    if (inside)
+        return false;
+
+    return true;
+}
+
+// PASS 0x1: RESOLVE() -> resolve.bin
+int64_t writeResolveRecord(FILE* f, int64_t offsetField, const string& text)
+{
+    // writes one [offset(8B)][size(4B)][string] record at the current file position
+    // returns this record's own starting byte position
+}
+int64_t readResolveRecord(FILE* f, string& outText)
+{
+    // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
+}
+int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)
+{
+    FuncEntry funcArray[MAX_FUNCS];
+    int32_t funcCount = 0;
+    PendingPatch patches[MAX_PATCHES];
+    int32_t patchCount = 0;
+    // Every source line becomes one record holding the raw line, as-is.
+    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
+    // (remember its position) and CALL (remember which function it needs
+    // and where its offset field sits).
+    // Once the whole file is written, every CALL's offset field is patched
+    // with its target's position. Patching happens after the full write
+    // Returns the byte offset of main's FUNC header record.
+    // if there is no main return the error 
+}
+
+// PASS 0x2: EXECUTION (tokenization happens here)
+enum TokenType
+{
+    KEYWORD,
+    IDENTIFIER,
+    PARAM
+};
+struct Token
+{
+    TokenType type;
+    string text;
+};
+int32_t tokenizeLine(const string& line, Token tokens[], int32_t maxTokens)
+{
+    // first word is always a instruction keyword
+    // instruction set = [func, func_end, call, set, add, sub, mul and div]
+    // next word is identifier like name of a function, variable name
+    // after identifier all are the params/arg, space separated
+}
+Snapshot* buildSnapshot(Stack<Frame>& callStack)
+{
+    // build the snapshot based on the callStack given
+}
+void executeProgram(const char* resolveBinPath, int64_t mainOffset, Timeline& timeline)
+{
+    // initialize the call stack
+    // make the main frame
+    // push main frame on the call stack
+
+    // implementation:
+    // execute line by line, and according to the keyword perform action
+}
+
+// PASS 0x3: SERIALIZE TIMELINE
+void writeTdbg(Timeline& timeline, const char* tdbgPath)
+{
+    // placeholder for header
+    // index array of the size of stepcount from the timeline
+    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
+    // after timeline add the index array i the file
+    // update the header
+}
+// main section
+int32_t main()
+{
+
+    if (!validateProgram("source.bin"))
+    {
+        // send an error response instead of a .tdbg file
+        return 1;
+    }
+
+    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
+
+    Timeline timeline;
+    executeProgram("resolve.bin", mainOffset, timeline);
+
+    writeTdbg(timeline, "session.tdbg");
+
+    return 0;
+}
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-miniPROJECT-1-phase1.cpp b/bscs25123-miniProject1-phase-1/bscs25123-miniPROJECT-1-phase1.cpp
new file mode 100644
index 0000000..e69de29
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj
new file mode 100644
index 0000000..edab76f
--- /dev/null
+++ b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj
@@ -0,0 +1,135 @@
+<?xml version="1.0" encoding="utf-8"?>
+<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
+  <ItemGroup Label="ProjectConfigurations">
+    <ProjectConfiguration Include="Debug|Win32">
+      <Configuration>Debug</Configuration>
+      <Platform>Win32</Platform>
+    </ProjectConfiguration>
+    <ProjectConfiguration Include="Release|Win32">
+      <Configuration>Release</Configuration>
+      <Platform>Win32</Platform>
+    </ProjectConfiguration>
+    <ProjectConfiguration Include="Debug|x64">
+      <Configuration>Debug</Configuration>
+      <Platform>x64</Platform>
+    </ProjectConfiguration>
+    <ProjectConfiguration Include="Release|x64">
+      <Configuration>Release</Configuration>
+      <Platform>x64</Platform>
+    </ProjectConfiguration>
+  </ItemGroup>
+  <PropertyGroup Label="Globals">
+    <VCProjectVersion>17.0</VCProjectVersion>
+    <Keyword>Win32Proj</Keyword>
+    <ProjectGuid>{1995b1e0-a77c-45b4-901e-02d4aba7b4d9}</ProjectGuid>
+    <RootNamespace>bscs25123miniProject1phase1</RootNamespace>
+    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
+  </PropertyGroup>
+  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
+  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'" Label="Configuration">
+    <ConfigurationType>Application</ConfigurationType>
+    <UseDebugLibraries>true</UseDebugLibraries>
+    <PlatformToolset>v143</PlatformToolset>
+    <CharacterSet>Unicode</CharacterSet>
+  </PropertyGroup>
+  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'" Label="Configuration">
+    <ConfigurationType>Application</ConfigurationType>
+    <UseDebugLibraries>false</UseDebugLibraries>
+    <PlatformToolset>v143</PlatformToolset>
+    <WholeProgramOptimization>true</WholeProgramOptimization>
+    <CharacterSet>Unicode</CharacterSet>
+  </PropertyGroup>
+  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
+    <ConfigurationType>Application</ConfigurationType>
+    <UseDebugLibraries>true</UseDebugLibraries>
+    <PlatformToolset>v143</PlatformToolset>
+    <CharacterSet>Unicode</CharacterSet>
+  </PropertyGroup>
+  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
+    <ConfigurationType>Application</ConfigurationType>
+    <UseDebugLibraries>false</UseDebugLibraries>
+    <PlatformToolset>v143</PlatformToolset>
+    <WholeProgramOptimization>true</WholeProgramOptimization>
+    <CharacterSet>Unicode</CharacterSet>
+  </PropertyGroup>
+  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
+  <ImportGroup Label="ExtensionSettings">
+  </ImportGroup>
+  <ImportGroup Label="Shared">
+  </ImportGroup>
+  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'">
+    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
+  </ImportGroup>
+  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|Win32'">
+    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
+  </ImportGroup>
+  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
+    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
+  </ImportGroup>
+  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
+    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
+  </ImportGroup>
+  <PropertyGroup Label="UserMacros" />
+  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'">
+    <ClCompile>
+      <WarningLevel>Level3</WarningLevel>
+      <SDLCheck>true</SDLCheck>
+      <PreprocessorDefinitions>WIN32;_DEBUG;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
+      <ConformanceMode>true</ConformanceMode>
+    </ClCompile>
+    <Link>
+      <SubSystem>Console</SubSystem>
+      <GenerateDebugInformation>true</GenerateDebugInformation>
+    </Link>
+  </ItemDefinitionGroup>
+  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'">
+    <ClCompile>
+      <WarningLevel>Level3</WarningLevel>
+      <FunctionLevelLinking>true</FunctionLevelLinking>
+      <IntrinsicFunctions>true</IntrinsicFunctions>
+      <SDLCheck>true</SDLCheck>
+      <PreprocessorDefinitions>WIN32;NDEBUG;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
+      <ConformanceMode>true</ConformanceMode>
+    </ClCompile>
+    <Link>
+      <SubSystem>Console</SubSystem>
+      <GenerateDebugInformation>true</GenerateDebugInformation>
+    </Link>
+  </ItemDefinitionGroup>
+  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
+    <ClCompile>
+      <WarningLevel>Level3</WarningLevel>
+      <SDLCheck>true</SDLCheck>
+      <PreprocessorDefinitions>_DEBUG;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
+      <ConformanceMode>true</ConformanceMode>
+    </ClCompile>
+    <Link>
+      <SubSystem>Console</SubSystem>
+      <GenerateDebugInformation>true</GenerateDebugInformation>
+    </Link>
+  </ItemDefinitionGroup>
+  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
+    <ClCompile>
+      <WarningLevel>Level3</WarningLevel>
+      <FunctionLevelLinking>true</FunctionLevelLinking>
+      <IntrinsicFunctions>true</IntrinsicFunctions>
+      <SDLCheck>true</SDLCheck>
+      <PreprocessorDefinitions>NDEBUG;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
+      <ConformanceMode>true</ConformanceMode>
+    </ClCompile>
+    <Link>
+      <SubSystem>Console</SubSystem>
+      <GenerateDebugInformation>true</GenerateDebugInformation>
+    </Link>
+  </ItemDefinitionGroup>
+  <ItemGroup>
+    <ClCompile Include="bscs25123-miniPROJECT-1-phase1.cpp" />
+    <ClCompile Include="bscs25123-TTDB-server.cpp" />
+  </ItemGroup>
+  <ItemGroup>
+    <None Include="source.bin" />
+  </ItemGroup>
+  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
+  <ImportGroup Label="ExtensionTargets">
+  </ImportGroup>
+</Project>
\ No newline at end of file
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj.filters b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj.filters
new file mode 100644
index 0000000..8783510
--- /dev/null
+++ b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj.filters
@@ -0,0 +1,30 @@
+﻿<?xml version="1.0" encoding="utf-8"?>
+<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
+  <ItemGroup>
+    <Filter Include="Source Files">
+      <UniqueIdentifier>{4FC737F1-C7A5-4376-A066-2A32D752A2FF}</UniqueIdentifier>
+      <Extensions>cpp;c;cc;cxx;c++;cppm;ixx;def;odl;idl;hpj;bat;asm;asmx</Extensions>
+    </Filter>
+    <Filter Include="Header Files">
+      <UniqueIdentifier>{93995380-89BD-4b04-88EB-625FBE52EBFB}</UniqueIdentifier>
+      <Extensions>h;hh;hpp;hxx;h++;hm;inl;inc;ipp;xsd</Extensions>
+    </Filter>
+    <Filter Include="Resource Files">
+      <UniqueIdentifier>{67DA6AB6-F800-4c08-8B7A-83BB121AAD01}</UniqueIdentifier>
+      <Extensions>rc;ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe;resx;tiff;tif;png;wav;mfcribbon-ms</Extensions>
+    </Filter>
+  </ItemGroup>
+  <ItemGroup>
+    <ClCompile Include="bscs25123-miniPROJECT-1-phase1.cpp">
+      <Filter>Source Files</Filter>
+    </ClCompile>
+    <ClCompile Include="bscs25123-TTDB-server.cpp">
+      <Filter>Source Files</Filter>
+    </ClCompile>
+  </ItemGroup>
+  <ItemGroup>
+    <None Include="source.bin">
+      <Filter>Resource Files</Filter>
+    </None>
+  </ItemGroup>
+</Project>
\ No newline at end of file
diff --git a/bscs25123-miniProject1-phase-1/source.bin b/bscs25123-miniProject1-phase-1/source.bin
new file mode 100644
index 0000000..d7f37c5
--- /dev/null
+++ b/bscs25123-miniProject1-phase-1/source.bin
@@ -0,0 +1,8 @@
+func foo b
+set a 10
+add b a
+func_end
+func main
+set k 10
+call foo k
+func_end
\ No newline at end of file









ON 8/10/26 :
diff --git a/bscs25123-miniProject1-phase-1/Readme.md b/bscs25123-miniProject1-phase-1/Readme.md
new file mode 100644
index 0000000..87d952a
--- /dev/null
+++ b/bscs25123-miniProject1-phase-1/Readme.md
@@ -0,0 +1,13 @@
+
+ON 5/10/26 WROTE THESE FUNCS:-
+
+
+
+
+
+
+
+
+
+
+ON 8/10/26 :
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
index 6116db2..8dece22 100644
--- a/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
+++ b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
@@ -43,32 +43,53 @@ class Stack
 public:
     // Implement these functions:
     Stack()
-    { // initialize the stack
+    { 
+        top = nullptr;
+        count = 0;
     }
     void push(const T& val)
     {
-
-        // pushes the value on the stack if max limit is not reached yet.
+        Node *n = new Node(val);
+        n->data = val;
+        n->next= top;
+        top = n;
+        count++;
+       
     }
     T pop()
     {
-        // pop the top value on the stack
+        if (top == nullptr)
+            return T();
+        Node *nn = top;
+        T val = nn->data;
+        top = top->next;
+        delete nn;
+        count--;
+        return val;
     }
     T& peek()
     {
-        // returns the top value on the stack
+        return top->data;
     }
     bool isEmpty()
     {
+        return top == nullptr;
     }
     int32_t depth()
     {
+        return count;
     }
     int32_t snapshot_into(T out[], int32_t maxLen)
     {
-        // copies every frame, top to bottom in the array given as a parameter
-        // this is what buildSnapshot() call, returns count written
+        Node* u = top;
+        int32_t count = 0;
+        for (int32_t i = 0; i < maxLen and u != nullptr; i++) {
+            out[i] = u->data;
+            u = u->next;
+            count++;
+        }
     }
+        return count;
 };
 
 
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj
index edab76f..2a2cdd9 100644
--- a/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj
+++ b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj
@@ -127,6 +127,7 @@
     <ClCompile Include="bscs25123-TTDB-server.cpp" />
   </ItemGroup>
   <ItemGroup>
+    <None Include="Readme.md" />
     <None Include="source.bin" />
   </ItemGroup>
   <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj.filters b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj.filters
index 8783510..bd11fe5 100644
--- a/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj.filters
+++ b/bscs25123-miniProject1-phase-1/bscs25123-miniProject1-phase-1.vcxproj.filters
@@ -26,5 +26,6 @@
     <None Include="source.bin">
       <Filter>Resource Files</Filter>
     </None>
+    <None Include="Readme.md" />
   </ItemGroup>
 </Project>
\ No newline at end of file


OCT 10
diff --git a/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
index 8dece22..3d37fc4 100644
--- a/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
+++ b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
@@ -110,16 +110,33 @@ class Timeline
     // Implement these functions
     Timeline()
     {
+        head = nullptr;
+        tail = nullptr;
+        stepCount = 0;
     }
     void record(Snapshot* s)
     {
-        // add record in the timeline
+        TimelineNode *k=new TimelineNode;
+        k->data = s;
+        k->next = nullptr;
+        k->prev = nullptr;
+        if (head==nullptr) {
+            head = tail = k;
+        }
+        else {
+            tail->next = k;
+             k->prev = tail;
+            tail = k;
+        }
+
     }
     TimelineNode* begin()
     {
+        return head;
     }
     int32_t getStepCount()
     {
+        return stepCount;
     }
 };
 
@@ -254,11 +271,39 @@ bool validateProgram(const char* sourcePath)
 // PASS 0x1: RESOLVE() -> resolve.bin
 int64_t writeResolveRecord(FILE* f, int64_t offsetField, const string& text)
 {
+    if (!f) {
+        return -1;
+    }
+    int64_t pos= ftell(f);
+    int64_t y = text.size();
+    fwrite(&offsetField, 8, 1, f);
+    fwrite(&y, 4, 1, f);
+    fwrite(text.c_str(), 1,y, f);
+    return pos;
     // writes one [offset(8B)][size(4B)][string] record at the current file position
     // returns this record's own starting byte position
 }
 int64_t readResolveRecord(FILE* f, string& outText)
 {
+    if (!f) {
+        return -1;
+    }
+  
+    int64_t g;
+   
+    if (fread(&g, 8, 1, f) != 1) {
+        return -1;
+    }
+    int32_t o;
+
+    if (fread(&o, 4, 1, f)!=1) {
+        return -1;
+    }
+    outText.resize(o);
+    if (fread(&outText[0],  1,o, f) != o) {
+        return -1;
+    }
+    return g;
     // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
 }
 int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)

 OCT 11
 diff --git a/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
index 3d37fc4..37af781 100644
--- a/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
+++ b/bscs25123-miniProject1-phase-1/bscs25123-TTDB-server.cpp
@@ -14,6 +14,7 @@
 #include <cstdint>
 #include <fstream>
 #include <cstdio>
+#include<vector>
 using namespace std;
 
 // ---- Constants ----
@@ -239,6 +240,18 @@ string secondWord(const string& line)
     }
     return secword;
 }
+string restAfterSecond(const string& line)
+{
+    int j = 0;
+    while (j < (int)line.size() && line[j] != ' ') 
+        j++;  
+    while (j < (int)line.size() && line[j] == ' ') 
+        j++;   
+    while (j < (int)line.size() && line[j] != ' ')
+        j++;  
+    return line.substr(j);                               
+}
+
 bool validateProgram(const char* sourcePath)
 {
     ifstream in(sourcePath, ios::binary);
@@ -306,12 +319,67 @@ int64_t readResolveRecord(FILE* f, string& outText)
     return g;
     // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
 }
-int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)
-{
-    FuncEntry funcArray[MAX_FUNCS];
-    int32_t funcCount = 0;
-    PendingPatch patches[MAX_PATCHES];
-    int32_t patchCount = 0;
+    int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)
+    {
+        FuncEntry funcArray[MAX_FUNCS];
+        int32_t funcCount = 0;
+        vector<string> l;
+        vector<int64_t> lineoff;
+        ifstream in(sourcePath, ios::binary);
+        if (!in) 
+            return -1;
+        int64_t offset = 0;
+        string line;
+        while (readSourceLine(in, line)) {
+            string kw = firstWord(line);
+            string name = secondWord(line);
+            string rest = restAfterSecond(line);
+            lineoff.push_back(offset);
+            l.push_back(line);
+            if (kw == "func") {
+                if (funcCount >= MAX_FUNCS) 
+                    return -1;
+                funcArray[funcCount].funcName = name;
+                funcArray[funcCount].byteOffsetInResolveBin = offset;
+                funcCount++;
+            }
+            int64_t size = line.size();
+            if (kw == "call")
+                size = 5 + 16 + rest.size();     
+            offset += 8 + 4 + size;
+        }
+        in.close();
+        FILE* out = fopen(resolveBinPath, "wb");
+        if (!out) 
+            return -1;
+        for (int32_t i = 0; i < (int32_t)l.size(); i++) {
+            string kw = firstWord(l[i]);
+            string name = secondWord(l[i]);
+            string rest = restAfterSecond(l[i]);
+            string toWrite = l[i];
+            if (kw == "call") {
+                int64_t tgt = -1;
+                for (int32_t j = 0; j < funcCount; j++) {
+                    if (funcArray[j].funcName == name)
+                        tgt = funcArray[j].byteOffsetInResolveBin;
+                }
+                if (tgt < 0) {
+                    fclose(out);
+                    return -1;
+                }
+                string dig = to_string(tgt);
+                string padd = string(16 - dig.size(), '0') + dig;
+                toWrite = "call " + padd + rest;
+            }
+            writeResolveRecord(out, lineoff[i], toWrite);
+        }
+        fclose(out);
+        for (int32_t j = 0; j < funcCount; j++) {
+            if (funcArray[j].funcName == "main")
+                return funcArray[j].byteOffsetInResolveBin;
+        }
+        return -1;
+    }
     // Every source line becomes one record holding the raw line, as-is.
     // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
     // (remember its position) and CALL (remember which function it needs
@@ -320,7 +388,7 @@ int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)
     // with its target's position. Patching happens after the full write
     // Returns the byte offset of main's FUNC header record.
     // if there is no main return the error 
-}
+
 
 // PASS 0x2: EXECUTION (tokenization happens here)
 enum TokenType


