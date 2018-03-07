--TEST--
Basic crashing and restarting of an actor with a ONE_FOR_ONE supervisor
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef, Supervisor};

$as = new ActorSystem(1);

class A extends Actor
{
    public function receive(){$this->receiveBlock();}
}

class B extends Actor
{
    private static $i = 0;

    public function __construct()
    {
        var_dump('Creating B');
    }
    public function receive()
    {
        $this->receiveBlock();
        if (self::$i++ < 3) {
            var_dump('Crashing B');
            throw new exception();
        } else {
            ActorSystem::shutdown();
        }
    }
}

class C extends Actor
{
    public function __construct()
    {
        for ($i = 0; $i < 4; ++$i) {
            $this->send('b', 1);
        }
    }
    public function receive(){}
}

$a = new ActorRef(A::class, [], 'a');
$b = new ActorRef(B::class, [], 'b');

$s = new Supervisor($a, Supervisor::ONE_FOR_ONE);
$s->addWorker($b);

$c = new ActorRef(C::class, [], 'c');
--EXPECT--
string(10) "Creating B"
string(10) "Crashing B"
string(10) "Creating B"
string(10) "Crashing B"
string(10) "Creating B"
string(10) "Crashing B"
string(10) "Creating B"
