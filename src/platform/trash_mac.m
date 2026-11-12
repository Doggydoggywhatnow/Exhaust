/* macOS: move an item to the Trash with NSFileManager. (At most 150 lines of
 * Objective-C are allowed in Exhaust, and only in src/platform/.) */
#import <Foundation/Foundation.h>

#include "exhaust_platform.h"

int ex_platform_trash_system(const char *path) {
    @autoreleasepool {
        NSString *p = [NSString stringWithUTF8String:path];
        NSURL *url = [NSURL fileURLWithPath:p];
        NSError *error = nil;
        return [[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:&error] ? 0 : -1;
    }
}
