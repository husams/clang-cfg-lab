// Part 8.3 -- Objective-C: messages and blocks (compile with: -- -x objective-c -fblocks -w)
int leaf(int x) { return x + 1; }

@interface Counter
- (int)bump:(int)x;
- (int)external:(int)x;
@end

// bump: is defined in this translation unit; external: is only declared
@implementation Counter
- (int)bump:(int)x { return leaf(x); }
@end

// a message to a method defined here is an edge, a message to a declared-only method is not
int send(Counter *c) { return [c bump:1] + [c external:2]; }

// an immediately invoked block literal is an edge to a block node
int immediate(void) { return ^(int x) { return leaf(x); }(3); }

// a call through a block variable is not
int via_var(void) {
  int (^b)(int) = ^(int x) { return leaf(x); };
  return b(4);
}
