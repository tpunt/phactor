# The Phactor Extension

This extension seeks to provide an implementation of the [Actor model](https://en.wikipedia.org/wiki/Actor_model) in PHP. Due to the size and complexity of this project, I am making an early-stage release of it, in hopes that it will attract the help of other developers.

Quick details:
 - Uses an N:M threading model
 - Cooperative scheduling

Requirements:
 - A ZTS version of PHP 7.2
 - An x86-64 Unix-based OS
 - The Pthread library

Major goals:
 - Stabilise things
 - Implement supervision trees
 - Implement remote actors
 - Implement internal non-blocking APIs

How you can help:
 - Resolve any of the open issues of this repo
 - Open new issues for: bugs, general feedback, support for a platform, ideas discussion, etc

This extension has been tested on OS X (Yosemite and Sierra), Centos 7 (64bit), Ubuntu 14.04 and 16.04 (64bit).

## The Basics

Each actorised application will have its own actor system. This actor system is responsible for managing the actors within its system, along with configuring it.

Each actor has a single entry point, `Actor::receive()`, that should be use to receive messages for that actor. When an actor is created, its `receive()` method will automatically be invoked. The actor will only be destroyed once its `receive()` method has finished executing. To keep an actor alive, looping should be used in tandem with the `Actor::receiveBlock()` method to voluntarily interrupt the actor, placing it in an idle state where it will wait for new messages to arrive.

The following short script demonstrates the creation of an actor, where it will keep sending itself random messages until a `'shutdown'` message is sent, where the actor system will then be shutdown (via `ActorSystem::shutdown()`).

```php
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem(1);

class Test extends Actor
{
    // internal actor state
    private $i = 0;
    private $messages = ['something 1', 'something 2', 'something 3', 'shutdown'];

    public function __construct(string $s)
    {
        $ref = ActorRef::fromActor($this);

        $this->send($ref, [$ref->getName(), $s]);
    }

    public function receive()
    {
        do {
            // block here, waiting for a new message to be received
            [$sender, $message] = $this->receiveBlock();
            var_dump($message);

            if ($message === 'shutdown') {
                break;
            }

            // send a message to itself
            $this->send($sender, [$sender, $this->messages[$this->i++]]);
        } while (true);

        // shut down the actor system - without this, the PHP process will not stop
        ActorSystem::shutdown();
    }
}

// spawn the new actor
new ActorRef(Test::class, ['something 0'], 'testing');
```

## API

```php
<?php

namespace Phactor;

final class ActorSystem
{
    public function __construct(int $threadCount = $coreCount + 10); // 10 async threads

    public static function shutdown(void) : void;

    public function block(void) : void; // should be ignored (used internally)
}

final class ActorRef
{
    private $ref;
    private $name;

    public function __construct(string $actorClassName[, array $ctorArgs[, string $actorName = '']]);

    public function getRef(void) : string;

    public function getName(void) : string;

    public static function fromActor(Actor $actor) : ActorRef;
}

abstract class Actor
{
    // the public API of an actor
    public abstract function receive(void) : void;

    // send a message to another actor
    protected final function send(ActorRef $toActorRef, mixed $message) : void;
    protected final function send(string $toActorName, mixed $message) : void;

    // wait for a message
    protected final function receiveBlock(void) : mixed; // returns $message
}
```

All messages and actor constructor arguments (the second parameter of `ActorRef::__construct`) will be serialised.
