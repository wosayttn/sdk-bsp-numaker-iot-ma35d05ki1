echo on

git.exe clone https://github.com/OpenNuvoton/MA35D1_NuWriter
IF %ERRORLEVEL% EQU 0 (
    echo "Failed to download NuWriter"
    exit 1
)

pip3 install -r requirements.txt
IF %ERRORLEVEL% EQU 0 (
    echo "Failed to download NuWriter"
    exit 1
)
