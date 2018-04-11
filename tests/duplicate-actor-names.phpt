--TEST--
Testing that serialising objects in the context copying code fails
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef, Supervisor};

class Test extends Actor
{
    public function receive() {$this->receiveBlock();}
}

class Test2 extends Actor
{
    public function receive() {}
}

$actorSystem = new ActorSystem();

new ActorRef(Test::class, [], 'a');

try {
    new ActorRef(Test::class, [], 'a');
} catch (Error $e) {
    var_dump($e->getMessage());
}

try {
    $s = new Supervisor(new ActorRef(Test2::class));
    $s->newWorker(Test::class, [], 'b');
} catch (Error $e) {
    // var_dump($e->getMessage()); // this may or may not be output
}

ActorSystem::shutdown();
--EXPECT--
string(57) "An actor with the specified name has already been created"
