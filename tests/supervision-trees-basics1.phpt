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
    public function __construct()
    {
        var_dump('Creating B');
    }
    public function receive()
    {
        $this->receiveBlock();
        var_dump('Crashing B');
        throw new exception();
    }
}

class C extends Actor
{
    public function __construct()
    {
        for ($i = 0; $i < 3; ++$i) {
            $this->send('b', 1);
        }
        ActorSystem::shutdown();
    }
    public function receive(){}
}

$a = new ActorRef(A::class, [], 'a');
$b = new ActorRef(B::class, [], 'b');

$s = new Supervisor($a, Supervisor::ONE_FOR_ONE, $b);

$c = new ActorRef(C::class, [], 'c');
--EXPECT--
string(10) "Creating B"
string(10) "Crashing B"
string(10) "Creating B"
string(10) "Crashing B"
string(10) "Creating B"
string(10) "Crashing B"
string(10) "Creating B"
