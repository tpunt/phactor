<?php

class Task
{
    public static function async(callable $task)
    {
        $taskClass = new class extends Actor {
            public function receive($task)
            {
                return $task();
            }
        };

        $taskClass->send($taskClass, $task);

        return $taskClass;
    }

    public static function await($taskClass)
    {
        return $taskClass->receiveBlock();
    }
}

$actorSystem = new ActorSystem();

$task = Task::async(function(){return "Testing\n";});
echo "Something\n";
echo Task::await($task);

$actorSystem->block();
