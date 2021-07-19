#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <sys/vfs.h>
// c++과 opencv에 관련된 
// 자동으로 코드의 앞에 cv,std를 붙여주는 코드
using namespace cv;
using namespace std;

// 윈도우창의 이름을 record로 한다.
#define VIDEO_WINDOW_NAME "record"
// 파일,폴더,로그 일때의 인자들 정의
#define TIME_FILENAME 0
#define FOLDER_NAME   1
#define LOG_TIME      2
// 폴더가 저장되는 최대 갯수
#define MAX_LIST 50 

// 시간이름함수 사용시 출력에 이용하는 버퍼
char tBUF[100];
// 오래된 폴더찾는 함수에서 쓰는 경로값
const char *path = "/home/pi/blackBox/data"; 

// 시간으로 된 이름을 만드는 함수
void getTime(int ret_type)
{
    // time_t는 데이터 타입으로 1970 부터 현재까지
    // 흐른 초수를 의미한다.
    time_t UTCtime;

    // time_t는 단순히 누적된 초이므로 사용하기 힘들어
    // tm 구조체의 멤버변수를 통해 날짜의 개념으로 표현한다.
    struct tm *tm;

    // time_t형 변수를 입력받아 부호환된 현재 시간을 반환한다.
    time(&UTCtime); // UTC 현재 시간 읽어오기

    // 위의 time을 통해 반환된 시간을 local함수를 통해
    // 변환하는 시간 값에 대한 포인터를 지정받고 
    // 친숙한 시간값의 tm구조체의 포인터로 반환하여 tm으로 받는다.
    tm = localtime(&UTCtime);
    
    // strftime 함수를 통해 우리가 원하는 형식에 맞추어 출력한다.
    // (시간이 저장되는 버퍼, 출력되는 문자열의 최대길이, 출력형식, 해석해야할 값)
    if (ret_type==TIME_FILENAME)
        strftime(tBUF,sizeof(tBUF),"%Y%m%d_%H%M%S.avi", tm);
    else if(ret_type==FOLDER_NAME)
        strftime(tBUF,sizeof(tBUF),"%Y%m%d%H", tm);
    else if(ret_type==LOG_TIME)
        strftime(tBUF,sizeof(tBUF),"[%Y-%m-%d %H:%M:%S]", tm);
}

// 용량을 확인한 후 용량을 반환하는 함수
float getRatio()
{
    int blocks;
    int avail;
    float ratio;
    // statfs는 파일시스템정보를 구조체로 반환하는
    // 함수로써 구조체내의 정보를 이용한다.
    // lstatfs라는 구조체변수 선언.
    struct statfs lstatfs;

    // "/"는 맨처음 루트디렉토리의 경로이름이다.
    // 즉 이 라즈베리파이의 전체디렉토리를 분석하여
    // 구조체 lstatfs에 채워넣는다.
    statfs("/", &lstatfs);

    // f_blocks는 파일시스템내 총 데이터 블럭들
    // f_bsize는 최적화된 전송 블럭 크기
    // f_bsize를 1024로 나누어 블럭하나의 용량으로 만든뒤
    // 곱하여 전체사용가능용량을 구한다.
    blocks = lstatfs.f_blocks * (lstatfs.f_bsize/1024);

    // f_bavail은 비-슈퍼 유저를 위한 여유 블럭들
    // 마찬가지로 곱하여 사용가능용량을 구한다.
    avail = lstatfs.f_bavail * (lstatfs.f_bsize/1024);

    // blocks는 전체사용가능 용량이 됨
    // avail은 사용가능한 용량이 됨
    // ratio는 사용가능한용량의 퍼센트가 됩니다.
    ratio = (avail *100) / blocks;

    // ratio를 f로 반환
    return ratio;
}

// ".", ".." 은 빼고 나머지 파일명 출력하는 필터 함수 
// static과 const 둘다 다른곳에서 
// 이 함수,객체를 수정하지 못하게 한다.
// dirent 구조체는 dirent.h에 정의된 구조체(i노드번호,파일명 갖는)로써
// 디렉토리의 d_name(이름)으로 폴더를 구분한다.
static int filter(const struct dirent *dirent)
{
    //filter함수의 반환값(여기서만사용됨)
    int result;
    // strcmp함수는 두 문자열을 비교해 
    // 같으면 0 반환 
    // 입력받은 구조체의 폴더이름이 ".",".."이면
    // 0이 나오나 !로인해 1이되고 하나라도 ".",".."이면
    // ||로 인해 ?에서 참이면 0, 거짓이면 1 result에 대입
    result = !(strcmp(dirent->d_name, ".")) || !(strcmp(dirent->d_name, "..")) ? 0 : 1;
    // ".",".." 이면 0반환, 둘 다 아니면 1 반환
    return result;
}

// 제일 오래된 폴더 찾는 함수
long searchOldFolder(void) 
{ 
    // dirent 구조체 이중포인터로 선언
    // 다음의 scandir 함수에 사용되는 구조체
    // scandir이 다중포인터를 사용함
    struct dirent **namelist; 
    // 폴더 수를 받는 변수
    int count; 
    // 폴더를 구분하는 변수
    int idx; 
    // 폴더이름을 숫자로 받기 때문에 long으로 선언
    // 제일 오래된 폴더가 들어가는 변수
    long min;
    // 폴더가 정의되는 배열
    long num[MAX_LIST];

    // 1st : 내가 탐색하고자 하는 폴더
    // 2nd : namelist를 받아올 구조체 주소값
    // 3rd : filter
    // 4th : 알파벳 정렬
    // scandir()함수에서 namelist 메모리를 malloc

    // scandir은 파라미터로 넘겨진 path경로에 있는 파일 및 폴더를
    // filter로 정제하여 alphasort(미리 만들어진 배열을 알파벳순서로 정렬)
    // 사용하여 위에서 정의한 namelist구조체에 저장합니다.
    // 반환값은 -1은 오류, 0이상은 namelist에 저장된 구조체의 갯수입니다.
    if((count = scandir(path, &namelist, *filter, alphasort)) == -1) 
    { 
        fprintf(stderr, "%s Directory Scan Error: %s\n", path, strerror(errno)); 
        return 1; 
    } 
    // 폴더갯수 몆개인지 체크
    printf("count=%d\n",count);    
    // idx변수를 사용하여 num배열에 atol함수를 이용하여
    // 위의 namelist의 폴더이름들을 변환하여 대입합니다.
    for(idx=0;idx<count;idx++)
    {
        num[idx] = atol(namelist[idx]->d_name);
    }
    // 처음 min값은 num배열의 첫번째 배열값
    min = num[0];     //min 초기화
    // min에 제일 오래된 폴더 찾는 부분
    for(idx = 0;idx<count;idx++)
    {
        if(num[idx] < min ) //num[idx]가 min보다 작다면
            min = num[idx]; //min 에는 num[idx]의 값이 들어감
    }

    // 건별 데이터 메모리 해제 
    for(idx = 0; idx < count; idx++) 
    { 
        free(namelist[idx]); 
    } 

    // namelist에 대한 메모리 해제 
    free(namelist); 
    // 제일오래된 폴더 반환
    return min; 
}

// 폴더를 삭제하는 함수, 삭제할 폴더의 경로와 오류조건을 받는다.
int rmdirs(const char *path) 
{ 
    // 폴더관련 인자들을 받기위해 구조체 포인터
    // dir_ptr을 DIR타입으로 선언한다.
    DIR * dir_ptr = NULL; 
    // 폴더의 이름을 갖는 구조체 dirent에 접근하는 file 변수 선언
    struct dirent *file = NULL; 
    // 파일 정보를 저장하는 구조체를 선언한다.
    struct stat buf; 
    // 파일이름을 저장하는 배열선언
    char filename[1024]; 
    // 성공시 우리가 지정한 디렉토리의 포인터값을 받습니다.
    if((dir_ptr = opendir(path)) == NULL) 
    { 
        //path가 디렉토리가 아니라면 삭제하고 종료합니다. 
        return unlink(path); 
    } 
    // 디렉토리의 처음부터 파일 또는 디렉토리명을 순서대로 한개씩 읽습니다.
    // 성공시 파일정보가 담긴 dirent 구조체를 반환받습니다.
    // 폴더내에 파일이 없을때까지 반복합니다.
    while((file = readdir(dir_ptr)) != NULL) { 
        // 파일을 삭제하기 위해 우리가 반복문에서 받은
        // file 구조체내의 d_name과 경로를 합쳐
        // filename 배열에 대입합니다.
        sprintf(filename, "%s/%s", path, file->d_name);
        // 파일을 삭제.
        unlink(filename);
    } 
    //open된 directory 정보를 close 합니다.
    closedir(dir_ptr); 
    // 폴더를 삭제하며 함수를 끝냅니다.
    return rmdir(path); 
}

int main(void)
{
    int deviceID = 0;
    // CAP_V4L2는 리눅스라는 뜻
    int apiID = cv::CAP_V4L2;
    // 프레임을 세는 카운트 변수
    int frameCount;
    // 한 영상의 최대 프레임 수
    int MaxFrame = 1780;
    // mat 이라는 opencv의 matrix 데이터타입선언
    // 영상은 2차원배열로 생각할 수 있다.
    Mat frame;
    // esc키가 눌렸을때의 flag
    int exitFlag = 0;
    // 폴더의 이름받는 인자
    char dirname[40];
    // ratio,사용가능용량퍼센트 받는 인자
    float diskratio;
    // search함수에서 나온 min값을 받는 변수
    long searchResult;
    // searchResult를 문자열로 받는 변수
    char foldername[40];
    // 폴더삭제할때 경로를 이름이랑 합치는 변수
    char delFoldername[40];
    // 파일생성시 사용하는 경로가 합쳐진 파일이름
    char filePath[100];
    // 로그파일의 오류를 알기위한 fd
    int fd;
    // 로그파일의 내용을 저장하는 버퍼
    char buff[200];
    // 로그파일의 바이트를 받는 변수
    int WRByte;
    // 로그에 추가하는 폴더이름 배열
    char logFolderName[100];
    // 로그에 추가하는 파일이름 배열
    char logFileName[100];

    // 로그파일을 기록하기 위해 파일열기
    // open은 파일의 경로,오픈모드,생성파일의 권한을 설정합니다.
    // 0644는 소유자가 읽기/쓰기 가능, 나머지는 읽기만 가능
    // WR=쓰기전용모드,CR=필요한경우파일생성,
    // TR=열었을때 이미 있는파일이고 쓰기옵션으로 열었다면 내용 모두 지움
    fd = open("/home/pi/blackBox/boxlog/blackbox.log",O_WRONLY | O_CREAT | O_TRUNC, 0644);
    getTime(LOG_TIME);
    sprintf(buff, "%s blackbox log파일 저장을 시작합니다.\n",tBUF);
    // write성공시 전달한 바이트 수를 반환합니다.
    // 데이터 전송영역 나타내는 파일 디스크립터
    // 전송할 데이터 가지는 버퍼
    // 데이터의 바이트 수
    WRByte = write(fd, buff, strlen(buff));

    // (1) 카메라, MicroSD등 필수 디바이스들이 
    //     정상적으로 인식되는지 확인한다.
    printf("-------디바이스들의 목록-------\n");
    system("lsusb");
    printf("\n");
    
    //  [1] VideoCapture("동영상파일의 경로")
    //      VideoCapture(0)
    //      videocapture 클래스 선언 cap이라는
    //      변수로 만든다

    //      Videocapture 클래스 객체는, 카메라
    //      또는 동영상 파일로부터 정지영상프레임을
    //      가져옵니다

    //      VideoWriter 클래스는 동영상 파일을 생성하고
    //      프레임을 저장하기 위해서 사용합니다.

    VideoCapture cap;
    VideoWriter writer;

    //  [2] 카메라 장치를 엽니다.
    //      디바이스 0번째를
    //      리눅스에서
    cap.open(deviceID, apiID);

    //  [3] 오류:카메라 열지 못하는 경우
    if (!cap.isOpened()) {
        perror("ERROR! Unable to open camera\n");
        return -1;
    }

    //  [4] 라즈베리파이 카메라의 해상도를 변경 
    cap.set(CAP_PROP_FRAME_WIDTH, 320);
    cap.set(CAP_PROP_FRAME_HEIGHT, 240);
    cap.set(CAP_PROP_FPS,30);

    //  [5] Video Recording
    //      현재 카메라에서 초당 몇 프레임으로 출력하고 있는가?
    float videoFPS = cap.get(CAP_PROP_FPS); //초당 프레임
    int videoWidth = cap.get(CAP_PROP_FRAME_WIDTH); //가로 크기
    int videoHeight = cap.get(CAP_PROP_FRAME_HEIGHT); //세로 크기

    //  [6] 초당프레임과 가로,세로 크기
    //      출력해서 확인함
    printf("videoFPS=%f\n",videoFPS);
    printf("width=%d, height=%d\n",videoWidth, videoHeight);
    printf("\n");

    while(1)    //계속 반복합니다.
    {
        //  (2) 현재 시간으로된 폴더를 생성합니다.
        // getTime 함수로 tBUF를 받습니다.
        getTime(FOLDER_NAME);
        // log 추가를 위해 값 복사
        sprintf(logFolderName,"%s",tBUF);
        // sprintf는 출력하는 결과 값을 변수에 저장하게 해줍니다.
        // getTime에서의 tBUF값을 가운데 인자에 추가하여
        // dirname에 저장합니다.
        sprintf(dirname, "/home/pi/blackBox/data/%s",tBUF);
        
        mkdir(dirname,0755);

        // 폴더생성시 로그파일에 내용추가
        getTime(LOG_TIME);
        sprintf(buff, "%s%s 이름으로 폴더를 생성합니다.\n",tBUF,logFileName);
        // fd는 생성할때 log파일 경로로 지정했음
        write(fd, buff, strlen(buff));

        //  (3) 녹화를 시작하기전에 디스크용량을 확인한다.
        //      용량이 부족할경우 blackBox폴더의 하위 디렉토리중
        //      가장 오래된 폴더를 삭제한다. 

        // diskratio인자에 함수반환값 대입 
        diskratio = getRatio();
        printf("현재사용가능용량은 [%f%] 입니다.\n");

        // 사용가능용량이 (30)%미만이면 오래된폴더를 삭제한다.
        if(diskratio<30.0)
        {
            // 삭제에 들어간다 알림
            printf("용량이 부족하므로 오래된 폴더를 삭제합니다.\n");
            // 오래된폴더이름을 받는다.
            searchResult = searchOldFolder();
            // 문자열로 변환한다.
            sprintf(foldername,"%d",searchResult);
            // 잘나오나 체크
            printf("가장오래된 폴더는 %s 입니다.\n",foldername);
            // 삭제하기위해 폴더주소와 합치고 
            // 다시 del변수에 대입한다.
            sprintf(delFoldername,"/home/pi/blackBox/data/%s",foldername);

            // 폴더를 삭제
            rmdirs(delFoldername);
        } 

        //  (4) 현재 시간으로된 녹화파일을 생성한다.

        // 시간정보를 읽어와서 파일명을 생성
        getTime(TIME_FILENAME);
        printf("FILENAME:%s\n",tBUF);
        // 로그를 위해 파일명 복사
        sprintf(logFileName, "%s",tBUF);
        // 이름과 경로를 합쳐 filePath에 넣는다.
        sprintf(filePath, "%s/%s",dirname,tBUF);
        // 파일이 만들어지는 부분 두줄짜리 코드로
        // 위에서 사용한 opencv의 videoWriter클래스를 사용합니다.
        // 총 다섯가지 인자를 받습니다.
        // 1. 저장할 동영상 파일이름
        // 2. 동영상 압축 코덱을 표현하는 4-문자코드
        // 3. 저장할 동영상의 초당 프레임 수
        // 4. 동영상 프레임의 가로 및 세로 크기
        // 5. 컬러인지 흑백인지(컬러면true,흑백이면false)
        
        // 코덱이란 코더-디코더의 약자로써 압축되지 않은 영상은
        // 크기가 매우 크기에 이 코덱을 사용하여 
        // 압축(인코딩)하고 압축해제(디코딩)하여
        // 영상을 봅니다. 이때 회사마다 만드는 코덱형식이 달라
        // 우리는 영상에 맞는 적절한 코덱을 사용하게 됩니다.
        writer.open(filePath, VideoWriter::fourcc('D','I','V','X'),
        videoFPS, Size(videoWidth, videoHeight), true);
        
        // 파일생성시 로그파일에 내용추가
        getTime(LOG_TIME);
        sprintf(buff, "%s     %s 이름으로 녹화를 시작합니다.\n",tBUF,logFileName);
        // fd는 생성할때 log파일 경로로 지정했음
        write(fd, buff, strlen(buff));

        //  [7] 동영상파일을 읽을 수 없는 오류
        if (!writer.isOpened())
        {
            perror("Can't write video");
            return -1;
        }
        frameCount =0;
        //  [8] 인자 두개의 함수인데
        //      하나만 입력했으므로
        //      영상에맞추어 창을 생성
        //      창의 이름은 위에 디파인했음
        //      VIDEO_WINDOW_NAME = record
        namedWindow(VIDEO_WINDOW_NAME);

        //  [9] 프레임을 0에서 maxframe 1780
        //      까지 이미지를 읽음
        while(frameCount<MaxFrame)
        {
            //  [10] 카메라에서 매 프레임(mat타입)
            //       마다 이미지 읽기
            cap.read(frame);
            frameCount++;
            //  [11] 카메라가 갑자기 빠지는 등의 오류 확인
            if (frame.empty()) {
                perror("ERROR! blank frame grabbed\n");
                break;
            }

            //  [12] 읽어온 한 장의 프레임을 writer에 쓰기
            //       writer는 클래스이다. 
            //       <<연산자를 통해 frame(mat형식)이 
            //       writer에 들어간다. 
            writer << frame; // test.avi

            //  [13] imshow(),이미지 창 이름, 파일 명을
            //       입력받아 이미지를 모니터에 보여줍니다.
            //       창이름은 디파인된 record, frame은
            //       말그대로 mat형식의 파일입니다.
            imshow(VIDEO_WINDOW_NAME, frame);

            //  [14] ESC=>27 'ESC' 키가 입력되면 종료 
            //       waitkey는 인자값으로 ms단위의 시간을
            //       입력받는데 여기서는 1000/30입니다
            //       이는 0.033초만 대기한다는 뜻인데
            //       우리가 프레임을 1초당 30프레임으로 
            //       설정했으므로 0.03초당 1프레임이므로
            //       정리하자면 이 함수는 딱 1프레임을 최대한
            //       맞추어 시간의 손실없이 기다리겠다는 뜻이며
            //       이 함수는 입력값을 출력하기 때문에 
            //       esc키(ascii_27)을 받으면 프린트문과 함께
            //       반복문을 빠져 나갑니다.
            if(waitKey(1000/videoFPS)==27)
            {
                printf("Stop video record\n");
                exitFlag = 1;  //flag는 나중에 다시 사용합니다.
                break;
            }

        }
        //  [15] 먼저 writer클래스를 해제(release) 합니다.
        //       다음 esc의 flag를 이용해 반복문을 나갑니다. 
        writer.release();
        if(exitFlag==1)
            break;
    }
    //  [16] 다음 cap클래스를 해제 합니다.
    //       그 후 destroy함수를 통해 VIDEO_WINDOW_NAME(def:record)창을
    //       파괴합니다.
    cap.release();
    destroyWindow(VIDEO_WINDOW_NAME);
    // log fd 닫음
    close(fd);

    return 0;
}