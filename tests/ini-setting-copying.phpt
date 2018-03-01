--TEST--
Testing the correct copying of ini settings
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

ini_set('error_reporting', 32767);
var_dump(ini_get('error_reporting'));
ini_set('error_reporting', 1);
var_dump(ini_get('error_reporting'));

$actorSystem = new ActorSystem();

new ActorRef(Test::class, [], 'Test');

class Test extends Actor
{
    public function receive()
    {
        var_dump(ini_get('error_reporting'));
        ActorSystem::shutdown();
    }
}
--EXPECT--
string(5) "32767"
string(1) "1"
string(1) "1"
