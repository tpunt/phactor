<?php

$actorSystem = new ActorSystem();

class User extends Actor
{
    private $name;

    public function __construct(string $name)
    {
        $this->name = $name;
    }

    public function receive($sender, $message)
    {
        switch ($message[0]) {
            case 'greeting':
                echo "Greetings to {$this->name}!\n-{$message[1]}\n";
                break;
        }
        // var_dump($sender);
    }

    public function getName()
    {
        return $this->name;
    }

    // public function __clone()
    // {
        // $this->name = $this->name;
    // }
}

$user1 = new User('Tom');
$user2 = new User('Liam');

new class($user1, $user2) extends Actor {
    public function __construct(User $user1, User $user2)
    {
        $user1->send($user2, ['greeting', $user1->getName()]);
    }

    public function receive($actor, $message){}
};

$actorSystem->block();
