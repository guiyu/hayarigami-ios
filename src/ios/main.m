#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include "../runtime/runtime.h"
#include <stdlib.h>
#include <stdio.h>

@interface GameSurface : UIView
@end
@implementation GameSurface
+ (Class)layerClass { return CAMetalLayer.class; }
@end
static void empty(void) {}
static void constructors(void) {
    for(size_t i=0;i<hy_module_count;i++)if(hy_modules[i].dynamic[12])
        ((void (*)(void))hy_range(&hy_modules[i],hy_modules[i].dynamic[12],4))();
}
static void *run_game(void *unused) {
    (void)unused;
    @autoreleasepool {
        NSString *resources=NSBundle.mainBundle.resourcePath;
        NSString *documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        NSString *save=[documents stringByAppendingPathComponent:@"Save"];
        NSError *error=nil;
        if(![NSFileManager.defaultManager createDirectoryAtPath:save withIntermediateDirectories:YES attributes:nil error:&error])hy_fail("save directory creation failed");
        setenv("HY_SAVE_DIR",save.fileSystemRepresentation,1);
        NSString *log=[documents stringByAppendingPathComponent:@"boot.log"];
        if(!freopen(log.fileSystemRepresentation,"a",stderr))hy_fail("cannot open local boot log");setvbuf(stderr,NULL,_IONBF,0);
        NSString *version=[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"];
        fprintf(stderr,"\nHayarigami build %s start\n",version.UTF8String);
        hy_install_signals();hy_load([resources stringByAppendingPathComponent:@"Modules"].fileSystemRepresentation,0);
        hy_thread_init();hy_thread_main();hy_filesystem_init([resources stringByAppendingPathComponent:@"RomFS"].fileSystemRepresentation);
        void (*start)(uint64_t,uint64_t,void (*)(void),void (*)(void),void (*)(void))=hy_symbol("_ZN2nn4init5StartEmmPFvvES2_S2_");
        if(!start)hy_fail("SDK Start missing");start(0x100,0,empty,constructors,empty);hy_fail("game main returned");
    }
}
@interface GameController : UIViewController {
    GameSurface *screen;NSMutableArray<UIButton *> *buttons;uint64_t pressed;
}
- (void)resetControls;
@end
@implementation GameController
- (void)viewDidLoad {
    [super viewDidLoad];self.view.backgroundColor=UIColor.blackColor;self.view.multipleTouchEnabled=YES;
    screen=[GameSurface new];screen.backgroundColor=UIColor.blackColor;[self.view addSubview:screen];buttons=[NSMutableArray new];
    NSArray *labels=@[@"◀",@"▲",@"▶",@"▼",@"A",@"B",@"X",@"Y",@"L",@"R",@"＋",@"－"];
    const unsigned bits[]={12,13,14,15,0,1,2,3,6,7,10,11};
    for(NSUInteger i=0;i<labels.count;i++) {
        UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];[button setTitle:labels[i] forState:UIControlStateNormal];button.tag=(NSInteger)(1ull<<bits[i]);
        button.backgroundColor=[UIColor.blackColor colorWithAlphaComponent:0.45];button.tintColor=UIColor.whiteColor;button.layer.cornerRadius=20;
        button.titleLabel.font=[UIFont boldSystemFontOfSize:19];
        [button addTarget:self action:@selector(down:) forControlEvents:UIControlEventTouchDown];
        [button addTarget:self action:@selector(up:) forControlEvents:UIControlEventTouchUpInside|UIControlEventTouchUpOutside|UIControlEventTouchCancel];
        [self.view addSubview:button];[buttons addObject:button];
    }
    hy_graphics_attach_layer((__bridge void *)screen.layer);hy_input_set(0,1);
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,8*1024*1024);pthread_t thread;
    if(pthread_create(&thread,&attr,run_game,NULL))hy_fail("game thread creation failed");pthread_attr_destroy(&attr);pthread_detach(thread);
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];CGRect bounds=self.view.bounds;CGFloat width=bounds.size.width,height=bounds.size.height;
    CGFloat game_width=MIN(width,height*16/9),game_height=game_width*9/16;screen.frame=CGRectMake((width-game_width)/2,(height-game_height)/2,game_width,game_height);
    UIEdgeInsets safe=self.view.safeAreaInsets;CGFloat left=safe.left+12,right=width-safe.right-12,base=height-safe.bottom-12;
    const CGFloat positions[12][2]={{0,1},{1,2},{2,1},{1,0},{-1,1},{-2,0},{-2,2},{-3,1},{0,3},{-1,3},{-1,0},{0,0}};
    for(NSUInteger i=0;i<buttons.count;i++) {
        CGFloat x=positions[i][0]>=0 ? left+positions[i][0]*46 : right+positions[i][0]*46;
        CGFloat y=base-(positions[i][1]+1)*46;buttons[i].frame=CGRectMake(x,y,42,42);
    }
}
- (void)resetControls {pressed=0;hy_input_set(0,1);}
- (void)down:(UIButton *)button {pressed|=(uint64_t)button.tag;hy_input_set(pressed,1);}
- (void)up:(UIButton *)button {pressed&=~(uint64_t)button.tag;hy_input_set(pressed,1);}
- (BOOL)prefersStatusBarHidden {return YES;}
- (BOOL)prefersHomeIndicatorAutoHidden {return YES;}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {return UIInterfaceOrientationMaskLandscape;}
@end
@interface GameSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation GameSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;if(![scene isKindOfClass:UIWindowScene.class])return;
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController=[GameController new];[self.window makeKeyAndVisible];
}
- (void)sceneWillResignActive:(UIScene *)scene {(void)scene;[(GameController *)self.window.rootViewController resetControls];hy_movie_set_active(0);hy_graphics_set_active(0);}
- (void)sceneDidBecomeActive:(UIScene *)scene {(void)scene;hy_movie_set_active(1);hy_graphics_set_active(1);}
@end
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AppDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)application;(void)options;UISceneConfiguration *configuration=[[UISceneConfiguration alloc] initWithName:@"Game" sessionRole:session.role];
    configuration.delegateClass=GameSceneDelegate.class;return configuration;
}
@end
int main(int argc,char **argv) {@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(AppDelegate.class));}}
