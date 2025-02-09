# blobsaver-cpp
The high-performance alternative to the blobsaver background task

## Quick Start:
 - Use [blobsaver](https://github.com/airsquared/blobsaver) to create config(Save Device) 
 - export config(blobsaver.xml) 
 ```
 blobsaver(airsquared) --export=.
 ```
 - Schedule the following command(crontab/systemd etc.) 
 ```
/path/to/blobsaver(this) -x /path/to/blobsaver.xml [-t /path/to/tsschecker -o /path/to/outputdir]
 ```

 ## [TODO](https://github.com/firedevel/blobsaver-cpp/issues/1)

 ## Credits
 [DeepSeek](https://www.deepseek.com/) for 70% of the work  
 [ChatGLM](https://chatglm.cn/) for 25% of the work  
 [FireDeveloper](https://github.com/firedevel) for 5% of the work