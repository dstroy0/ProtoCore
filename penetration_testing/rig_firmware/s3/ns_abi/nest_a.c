// Can a layer object embed another module's namespace struct BY VALUE, when that struct lives in a
// different TU? A static initializer needs a constant expression, and another object's value is not
// one - so this is the question that decides whether the chain is network.route.add() or
// network.route->add().
typedef struct
{
    void (*add)(void);
} RouteNs;

extern const RouteNs Route; // defined in the other TU

typedef struct
{
    void (*init)(void);
    RouteNs route;
} NetworkNs;

static void init(void)
{
}

const NetworkNs network = {init, Route};
