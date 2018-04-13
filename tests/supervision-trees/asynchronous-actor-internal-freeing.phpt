--TEST--
Ensure the internal part of an actor is freed asynchronously
--DESCRIPTION--
The internal part of an actor (specifically the VM stack stuff) must be freed
by the thread that allocated it, which means this operation needs to be
performed asynchronously.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef, Supervisor};

$as = new ActorSystem();

class A extends Actor
{
    private static $i = 0;

    public function receive()
    {
        $this->receiveBlock();

        if (self::$i++ < 3) {
            var_dump('Crashing A');
            throw new exception();
        } else {
            ActorSystem::shutdown();
        }
    }
}

class B extends Actor
{
    public function receive()
    {
        $this->receiveBlock();

    }
}

class C extends Actor
{
    public function __construct()
    {
        for ($i = 0; $i < 4; ++$i) {
            $this->send('a', 1);
        }
    }
    public function receive(){}
}

$s2 = new Supervisor(new ActorRef(A::class, [], 'a2'), Supervisor::ONE_FOR_ONE);
$a = new ActorRef(A::class, [], 'a');
$s2->addWorker($a);
$s = new Supervisor($a, Supervisor::ONE_FOR_ONE);
$s->addWorker(new ActorRef(B::class));
$s->addWorker(new ActorRef(B::class));
$s->addWorker(new ActorRef(B::class));

new ActorRef(C::class);
--EXPECT--
string(10) "Crashing A"
string(10) "Crashing A"
string(10) "Crashing A"
