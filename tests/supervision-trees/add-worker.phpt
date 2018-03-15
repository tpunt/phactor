--TEST--
Test the Supervisor::addWorker() method
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef, Supervisor};

$as = new ActorSystem(1);

class A extends Actor
{
    public function receive(){$this->receiveBlock();}
}

class B1 extends Actor
{
    private static $i = 0;

    public function __construct()
    {
        var_dump('Creating B');
        $this->send('b1', 1);
    }

    public function receive()
    {
        $this->receiveBlock();

        if (++self::$i !== 2) {
            throw new Exception();
        }
    }
}

class B2 extends Actor
{
    private static $i = 0;

    public function __construct()
    {
        var_dump('Creating B');
        $this->send('b2', 1);
    }

    public function receive()
    {
        $this->receiveBlock();

        if (++self::$i === 13) {
            ActorSystem::shutdown();
        } else {
            throw new Exception();
        }
    }
}

$a = new ActorRef(A::class, [], 'a');
$b1 = new ActorRef(B1::class, [], 'b1');
$b2 = new ActorRef(B2::class, [], 'b2');
$s = new Supervisor($a, Supervisor::ONE_FOR_ONE);
$s->addWorker($b1);
$s->addWorker('b2');
--EXPECT--
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
