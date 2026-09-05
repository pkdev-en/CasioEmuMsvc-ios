// GameViewController.h
// Objective-C++ shell cho iOS, tương đương vai trò của Game.java (Android).
// Toàn bộ logic thật nằm trong game_controller_core.hpp/.cpp (C++ thuần).

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface GameViewController : UIViewController

// Đường dẫn ROM truyền vào khi mở app (tương đương getArguments() bên Android)
@property (nonatomic, copy, nullable) NSString *modelPath;

@end

NS_ASSUME_NONNULL_END
