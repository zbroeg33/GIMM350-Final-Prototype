using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Enemy : MonoBehaviour {

    public float health;
    public int range;
    public float attackDamage;
    public float damage;

    UnityEngine.AI.NavMeshAgent agent;
    private Transform player;
    private bool isDead;

	// Use this for initialization
	void Start () {
        isDead = false;
        agent = GetComponent<UnityEngine.AI.NavMeshAgent>();
      player = GameObject.FindGameObjectWithTag("Player").transform;
	}
    public void OnTriggerEnter(Collider other)
    {
        
        TakeDamage(damage);
    }
    public void TakeDamage(float damage)
    {
        if (isDead)
        {
            return;
        }

        health -= damage;
        if (health <= 0)
        {
            Debug.Log(health);
            isDead = true;
            StartCoroutine("DestroyMonster");
        }
    }

    IEnumerator DestroyMonster()
    {
        yield return new WaitForSeconds(1.5f);
        Destroy(gameObject);
            
    }
	// Update is called once per frame
	void Update () {
		if (isDead)
        {
            return;
        }

        transform.LookAt(player);
        agent.SetDestination(player.position);

	}
}
