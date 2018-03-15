--TEST--
Test the restarting of an actor (for its receive() method)
--DESCRIPTION--
If an actor fails to invoke its receive() method then test restarting it. If
the receiveBlock() method is invoked, then the actor's restart count is reset.
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
    public function __construct(){var_dump('Creating B');}
    public function receive(){$this->receiveBlock();}
}

class C extends Actor
{
    private static $i = 0;

    public function __construct()
    {
        var_dump('Creating C');
        $this->send('c', 1);
    }

    public function receive()
    {
        $this->receiveBlock(); // blocking resets the current restart count streak

        if (++self::$i < 10) { // must be higher that the current default for maximum restarts
            throw new Exception();
        }

        ActorSystem::shutdown();
    }
}

$a = new ActorRef(A::class, [], 'a');
$s = new Supervisor($a, Supervisor::ONE_FOR_ONE);
$b = $s->newWorker(B::class, [], 'b');
$s2 = new Supervisor($b, Supervisor::ONE_FOR_ONE);
$c = $s2->newWorker(C::class, [], 'c');
--EXPECT--
string(10) "Creating B"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
string(10) "Creating C"
