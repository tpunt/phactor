# The Phactor Extension

This extension seeks to provide an implementation of the [Actor model](https://en.wikipedia.org/wiki/Actor_model) in PHP. Due to the size and complexity of this project, I am making an early-stage release of it, in hopes that it will attract the help of other developers.

Relevant reading:
 - [Actor model (Wikipedia)](https://en.wikipedia.org/wiki/Actor_model)
 - [The Reactive Manifesto](https://www.reactivemanifesto.org)

Quick details:
 - Uses an N:M threading model
 - Cooperative scheduling of actors
 - Actors have an initial cost of ~500 bytes

Requirements:
 - A ZTS version of PHP 7.2 (master branch is currently unsupported)
 - An x86-64 Unix-based OS
 - The Pthread library

Major goals:
 - Stabilise things (ongoing)
 - Implement supervision trees (in progress)
 - Implement remote actors (to do)
 - Implement internal non-blocking APIs (to do)

How you can help:
 - Resolve any of the open issues of this repo
 - Open new issues for: bugs, general feedback, support for a platform, ideas discussion, etc

This extension has been tested on OS X (Yosemite and Sierra), Centos 7 (64bit), and Ubuntu 14.04 and 16.04 (64bit).

## The Basics

Each actorised application will have its own actor system. This actor system is responsible for managing the actors within its system, along with configuring it.

Each actor has a single entry point: `Actor::receive()`. This method will be automatically invoked upon actor creation, where the actor will then be able to begin handling messages from its mailbox. Once the `Actor::receive()` method has finished executing, the actor will be destroyed. This means that in order to keep an actor alive, the `Actor::receive()` method needs to keep executing. This can be achieved by invoking the `Actor::receiveBlock()` method (perhaps in tandem with looping) to voluntarily interrupt the actor, placing it in an idle state where it will wait for new messages to arrive.

To shut down an actor system, the `ActorSystem::shutdown()` static method must be invoked (it can be called from anywhere in the application).

The following script demonstrates the basic flow of execution when using an actor:
```php
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    // internal actor state
    private $str;

    public function __construct(string $param)
    {
        $this->str = $param;
        // send a message to itself
        $this->send('actor name', "{$param} {$param}");
    }

    // automatically invoked (at an arbitrary time after __construct)
    public function receive()
    {
        var_dump($this->str); // string(5) "arg 1"

        $message = $this->receiveBlock(); // wait here for a new message

        var_dump($message); // string(11) "arg 1 arg 1"

        ActorSystem::shutdown(); // shut down the actor system

        // end of receive() method - the actor will be destroyed (asynchronously)
    }
}

// spawn the new actor - executes the actor's __construct() and receive() methods
new ActorRef(Test::class, ['arg 1'], 'actor name');
```

## API

```php
<?php

namespace Phactor;

final class ActorSystem
{
    public function __construct(int $threadCount = $coreCount + 10); // 10 additional threads

    public static function shutdown(void) : void;

    public function block(void) : void; // should be ignored (used internally)
}

final class ActorRef
{
    private $ref;
    private $name = '';

    public function __construct(string $actorClassName[, array $ctorArgs[, string $actorName]]);

    // for debugging only - do not use this for the sender (since it will be treated as the actor name)
    public function getRef(void) : string;

    public function getName(void) : string;

    // for retrieving a reference to an actor object
    public static function fromActor(Actor $actor) : ActorRef;
}

abstract class Actor
{
    // the public API and entry point of an actor
    public abstract function receive(void) : void;

    // send a message (asynchronously) to an actor
    protected final function send(ActorRef $toActorRef, mixed $message) : void;
    protected final function send(string $toActorName, mixed $message) : void;

    // wait for a message (cooperatively yields the actor)
    protected final function receiveBlock(void) : mixed;
}

final class Supervisor
{
    // supervision strategy
    public const ONE_FOR_ONE = 0;

    public function __construct(ActorRef|string $supervisor[, int $strategy = Supervisor::ONE_FOR_ONE]);

    // Add a pre-existing actor to the group of supervised workers
    public function addWorker(ActorRef|string $worker) : void;

    // Creates a new actor, links it to the supervisor, and return its actor reference.
    // This should be used if the actor's constructor may throw an exception
    public function newWorker(string $actorClass[, array $ctorArgs[, string $actorName]]) : ActorRef;
}
```

All messages and actor constructor arguments (the second parameter of `ActorRef::__construct` and `Supervisor::newWorker`) will be serialised.

## Examples

An anonymous actor sending a message to itself:
```php
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function __construct()
    {
        // since this is an anonymous actor, its reference must be fetched in
        // order to send a message to itself
        $actorRef = ActorRef::fromActor($this);
        $this->send($actorRef, 123);
    }

    public function receive()
    {
        var_dump($this->receiveBlock()); // int(123)
        ActorSystem::shutdown();
    }
}

// spawn a new anonymous actor
new ActorRef(Test::class);
```

An actor sending messages to itself in a synchronous execution style:
```php
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    private $messages = ['something 1', 'something 2', 'something 3', 'shutdown'];

    public function __construct(string $s)
    {
        $this->send('testing', $s);
    }

    public function receive()
    {
        $i = 0;

        do {
            // block here, waiting for a new message to be received
            $message = $this->receiveBlock();
            var_dump($message);

            if ($message === 'shutdown') {
                break;
            }

            // send a message to itself again
            $this->send('testing', $this->messages[$i++]);
        } while (true);

        // shut down the actor system - without this, the PHP process will not stop
        ActorSystem::shutdown();
    }
}

// spawn the new actor
new ActorRef(Test::class, ['something 0'], 'testing');
```

Using a supervisor with the `ONE_FOR_ONE` supervision strategy to handle the crashing of an actor (by automatically restarting it):
```php
<?php

use phactor\{ActorSystem, Actor, ActorRef, Supervisor};

$as = new ActorSystem(1);

// the supervisor
class A extends Actor
{
    public function receive(){$this->receiveBlock();}
}

// the worker
class B extends Actor
{
    private static $i = 0;

    public function __construct()
    {
        if (self::$i === 0) {
            ++self::$i; // make it crash once here
            var_dump('Crashing B in __construct()');
            throw new exception();
        }
    }

    public function receive()
    {
        if (self::$i === 1) {
            ++self::$i; // make it crash once here
            var_dump('Crashing B in receive()');
            throw new exception();
        }

        $this->send('b', 1); // send itself a message to resume after the following interrupt

        $this->receiveBlock(); // interrupt and wait for the int(1) message to arrive

        if (self::$i === 2) {
            ++self::$i; // make it crash once here
            var_dump('Crashing B in receive() again');
            throw new exception();
        }

        var_dump('Made it!');

        ActorSystem::shutdown();
    }
}

$a = new ActorRef(A::class, [], 'a');
$s = new Supervisor($a);

$b = $s->newWorker(B::class, [], 'b');
```

**Note:** whilst static variables have been used in the above example (to crash an actor a controlled number of times), static state is very unpredictable and should be avoided in such concurrent applications.

Whenever an actor is restarted, a new actor object will be created (so the `__construct` and `receive` methods will both be invoked again). The restarted actor will have the same reference and (if provided) the same name.

Because the actor could crash inside of its constructor, we created the new actor using the `Supervisor::newWorker()` method. This enables for an actor to be automatically restarted if it failed to construct itself (currently, the restart limit is hard coded to `5` - this will become configurable in future).
