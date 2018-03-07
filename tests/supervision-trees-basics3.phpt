--TEST--
Test the new restart limiting of an actor
--DESCRIPTION--
If an actor fails to invoke its constructor or receive() method 5 times in a row
(without invoking receiveblock(), that is), then let it crash permanently.
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
$b1 = $s->newWorker(B1::class, [], 'b1');
$b2 = $s->newWorker(B2::class, [], 'b2');
$b3 = $s->newWorker(B3::class, [], 'b3');
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
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
string(10) "Creating B"
