--TEST--
Basic crashing and restarting of an actor (from within its constructor) with a
ONE_FOR_ONE supervisor
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
    public function __construct()
    {
        var_dump('Creating B');
        throw new Exception();
    }

    public function receive(){}
}

class B2 extends Actor
{
    public function __construct()
    {
        var_dump('Creating B');
    }

    public function receive()
    {
        throw new Exception();
    }
}

class B3 extends Actor
{
    private static $i = 0;

    public function __construct()
    {
        var_dump('Creating B');
        $this->send('b3', 1);
    }

    public function receive()
    {
        $this->receiveBlock(); // blocking resets the current restart count streak

        if (++self::$i === 10) { // must be higher that the current default for maximum restarts
            ActorSystem::shutdown();
        } else {
            throw new Exception();
        }
    }
}

$a = new ActorRef(A::class, [], 'a');
$s = new Supervisor($a, Supervisor::ONE_FOR_ONE);
$b1 = $s->spawn(B1::class, [], 'b1');
$b2 = $s->spawn(B2::class, [], 'b2');
$b3 = $s->spawn(B3::class, [], 'b3');
--EXPECTF--
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
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
