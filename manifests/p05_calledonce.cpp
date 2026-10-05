// Part 5.5 -- called-once parameters in C++: the attribute is ignored, the naming convention is not.
typedef void (^Handler)(void);

// Attribute spelled out: Clang 22 says "'called_once' attribute ignored" in C++.
void attribute_ignored(int c, Handler h __attribute__((called_once))) {
  if (c)
    h();
}

// No attribute, but the parameter is called 'completionHandler': the convention check still sees it.
void conventional(int c, Handler completionHandler) {
  if (c)
    completionHandler();
}
