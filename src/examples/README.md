Websockets+ examples
====================

* example.cpp: simple request-reply
* example-streaming.cpp: streaming
* example-http.cpp: sends either html or file
* image-stream: stream images to web browser clients 
** stream sequence of images of various formats (jpeg, webp, png) and
   and size (up to 4k), use the included .html files as clients
** stream opengl buffer as image (same clients as previous item)   
** webgl: stream image to WebGL texture
* osg: full osgviewer with interactions implemented as a streaming server +
  web client; client receives OpenGL buffer and sends mouse, keyboard and
  window events


