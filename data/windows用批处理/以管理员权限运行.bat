@echo off
mode con lines=27 cols=60
:main
cls
color 2f
echo √√    √√  √√√      √√√√  √√√√√    √√√√
echo   √    √  √      √  √      √  √  √  √  √      √
echo   √    √  √      √  √              √      √
echo   √√√√  √      √    √√          √        √√
echo   √    √  √      √        √        √            √
echo   √    √  √      √          √      √              √
echo   √    √  √      √  √      √      √      √      √
echo √√    √√  √√√    √√√√      √√√    √√√√
echo.----------------------------------------------------------- 
echo.如有360等安全软件提醒，请勾选允许和不再提醒！
echo.
echo.警告：执行该命令你的原hosts将被覆盖！
echo.
echo.老D博客   LAOD.CN    LAOD.ORG    LAOD.TOP
echo.2016 永久更新地址：
echo.http://laod.cn/hosts/2016-google-hosts.html
color 2e
echo.-----------------------------------------------------------
echo.请选择使用：
echo.
echo.         1.使用智能host
echo.
echo.         2.恢复初始host	
echo.-----------------------------------------------------------

set /p choice=请输入:

echo.
  if %choice%==1 goto host DNS
  if %choice%==2 goto CL
     cls
     goto end
:host DNS
cls
color 2f
copy "%~dp0hosts" "%SystemRoot%\System32\drivers\etc\hosts"
ipconfig /flushdns
echo.-----------------------------------------------------------
echo.
echo 老D恭喜您，覆盖本地hosts并刷新本地DNS解析缓存成功!
echo.
echo 现在去打开Google、Twitter、Facebook、Gmail、谷歌学术吧！
echo.
@Pause
goto end

:CL
cls
color 2f
@echo 127.0.0.1 localhost > %SystemRoot%\System32\drivers\etc\hosts
echo 恭喜您，hosts恢复初始成功!
@Pause
goto end