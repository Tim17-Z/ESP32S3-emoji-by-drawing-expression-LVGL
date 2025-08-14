# ESP32S3-emoji-by-drawing-expression-LVGL
[Tim]
使用Esp32s3-1.85c套件利用lvgl显示表情测试，实现了绘制表情、随机时间间隔眨眼、概率连续双眨眼。<br>
<img width="510" height="556" alt="image" src="https://github.com/user-attachments/assets/417b158c-1191-4be2-9f00-189ab3d1d77d" /><br>
*操作步骤<br>
1.获取源文件，套件源项目链接：https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-1.85C/ESP32-S3-Touch-LCD-1.85C-Demo.zip<br>
2.移植eyesblink文件：在VScode IDF 打开源文件后找到LVGL_UI新建.c和.h文件即可。<br>
3.确定emoji被调用：在LVGL_Example内有调用函数语句。<br>
4.配置好编译环境：首先.vscode下新建一个c_cpp_properties.jason文件，根据自己的路径配置好。然后在main文件内的CMakeList配置好要编译的.c文件。具体配置问AI，很快就能解决。<br>
5.构建烧录：记得把芯片型号、端口、USART烧录方式配置好之后，再点击构建，最后烧录。<br>
*目的<br>
调通代码、熟悉IDF、获得一个可塑性高的可爱的玩具<br>
